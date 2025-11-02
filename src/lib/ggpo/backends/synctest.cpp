/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include "synctest.h"

void ___GGPOlogSync(const char* msg)
{
	const char* ipAddress = "127.0.0.1";
	unsigned short port = 17474;
	const char* message = msg;
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0) 
	{
		return;
	}
	SOCKET sendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sendSocket == INVALID_SOCKET) 
	{
		WSACleanup();
		return;
	}
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(port);
	if (inet_pton(AF_INET, ipAddress, &destAddr.sin_addr) <= 0) 
	{
		closesocket(sendSocket);
		WSACleanup();
		return;
	}
	int sendResult = sendto(sendSocket, message, strlen(message), 0, (sockaddr*)&destAddr, sizeof(destAddr));
	if (sendResult == SOCKET_ERROR) 
	{
		closesocket(sendSocket);
		WSACleanup();
		return;
	}
	closesocket(sendSocket);
	WSACleanup();
}

void ggnetlog(const char* format, ...) {
	static char buffer[1024]; // no more random char buffers everywhere.
	va_list args;
	va_start(args, format);
	vsnprintf_s(buffer, 1024, format, args);
	___GGPOlogSync(buffer);
	va_end(args);
}

SyncTestBackend::SyncTestBackend(GGPOSessionCallbacks *cb,
                                 char *gamename,
                                 int frames,
                                 int num_players) :
   _sync(NULL)
{
   _callbacks = *cb;
   _num_players = num_players;
   _check_distance = frames;
   _last_verified = 0;
   _rollingback = false;
   _running = false;
   _logfp = NULL;
   _current_input.erase();
   strcpy_s(_game, gamename);

   /*
    * Initialize the synchronziation layer
    */
   Sync::Config config = { 0 };
   config.callbacks = _callbacks;
   config.num_prediction_frames = MAX_PREDICTION_FRAMES;
   _sync.Init(config);

   /*
    * Preload the ROM
    */
   _callbacks.begin_game(gamename);
}

SyncTestBackend::~SyncTestBackend()
{
}

GGPOErrorCode
SyncTestBackend::DoPoll(int timeout)
{
   if (!_running) {
      GGPOEvent info;

      info.code = GGPO_EVENTCODE_RUNNING;
      _callbacks.on_event(&info);
      _running = true;
   }
   return GGPO_OK;
}

GGPOErrorCode
SyncTestBackend::AddPlayer(GGPOPlayer *player, GGPOPlayerHandle *handle)
{
   if (player->player_num < 1 || player->player_num > _num_players) {
      return GGPO_ERRORCODE_PLAYER_OUT_OF_RANGE;
   }
   *handle = (GGPOPlayerHandle)(player->player_num - 1);
   return GGPO_OK;
}

GGPOErrorCode
SyncTestBackend::AddLocalInput(GGPOPlayerHandle player, void *values, int size)
{
   if (!_running) {
      return GGPO_ERRORCODE_NOT_SYNCHRONIZED;
   }

   int index = (int)player;
   for (int i = 0; i < size; i++) {
      _current_input.bits[(index * size) + i] |= ((char *)values)[i];
   }
   return GGPO_OK;
}

GGPOErrorCode
SyncTestBackend::SyncInput(void *values,
                           int size,
                           int *disconnect_flags)
{
   BeginLog(false);
   if (_rollingback) {
      _last_input = _saved_frames.front().input;
   } else {
      if (_sync.GetFrameCount() == 0) {
         _sync.SaveCurrentFrame();
      }
      _last_input = _current_input;
   }
   memcpy(values, _last_input.bits, size);
   if (disconnect_flags) {
      *disconnect_flags = 0;
   }
   return GGPO_OK;
}

GGPOErrorCode
SyncTestBackend::IncrementFrame(void)
{  
   _sync.IncrementFrame();
   _current_input.erase();
   
   Log("SyncTestBackend End of frame(%d)...\n", _sync.GetFrameCount());
   EndLog();

   if (_rollingback) {
      Log("leaving via rollback");
      return GGPO_OK;
   }

   int frame = _sync.GetFrameCount();
   // Hold onto the current frame in our queue of saved states.  We'll need
   // the checksum later to verify that our replay of the same frame got the
   // same results.
   SavedInfo info;
   info.frame = frame;
   info.input = _last_input;
   info.cbuf = _sync.GetLastSavedFrame().cbuf;
   info.buf = (char *)malloc(info.cbuf);
   memcpy(info.buf, _sync.GetLastSavedFrame().buf, info.cbuf);
   info.checksum = _sync.GetLastSavedFrame().checksum;
   _saved_frames.push(info);

   if (frame - _last_verified == _check_distance) {
      // We've gone far enough ahead and should now start replaying frames.
      // Load the last verified frame and set the rollback flag to true.
      _sync.LoadFrame(_last_verified);

      _rollingback = true;
      while(!_saved_frames.empty()) {
         Log("calling _callbacks.advance_frame(0)");
         _callbacks.advance_frame(0);
         Log("left _callbacks.advance_frame(0)");

         // Verify that the checksumn of this frame is the same as the one in our
         // list.

         Log("getting info??");
         info = _saved_frames.front();
         Log("popping saved frame????");
         _saved_frames.pop();

         Log("checking frame count on info");

         if (info.frame != _sync.GetFrameCount()) {
            Log("Frame number %d does not match saved frame number %d", info.frame, frame);
            RaiseSyncError("Frame number %d does not match saved frame number %d", info.frame, frame);
         }

         Log("got past first check");

         int checksum = _sync.GetLastSavedFrame().checksum;
         if (info.checksum != checksum) {
            LogSaveStates(info);
            Log("Checksum for frame %d does not match saved (%08X != %08X)", frame, checksum, info.checksum);
            RaiseSyncError("Checksum for frame %d does not match saved (%08X != %08X)", frame, checksum, info.checksum);

			_callbacks.compare_buffers((unsigned char*)_sync.GetLastSavedFrame().buf, (unsigned char*)info.buf);
         }

         Log("got past second check");
         printf("Checksum %08d for frame %d matches.\n", checksum, info.frame);

         free(info.buf);

         Log("got past the free");
      }
      _last_verified = frame;
      _rollingback = false;
   }

   Log("leaving normally");

   return GGPO_OK;
}

void
SyncTestBackend::RaiseSyncError(const char *fmt, ...)
{
   /*char buf[1024];
   va_list args;
   va_start(args, fmt);
   vsprintf_s(buf, ARRAY_SIZE(buf), fmt, args);
   va_end(args);

   puts(buf);
   OutputDebugStringA(buf);
   EndLog();
   DebugBreak();
   */


   constexpr int numBackTicks = 1;
	static char buffer[2048] = {'`'};
	va_list args;
	va_start(args, fmt);
	//vsnprintf(buffer+numBackTicks, 1024-numBackTicks, fmt, args); // why cant i use the safe version here?? only god knows
	vsprintf_s(buffer+numBackTicks, 1024-numBackTicks, fmt, args); // why cant i use the safe version here?? only god knows)
   ___GGPOlogSync(buffer);
	va_end(args);

}

GGPOErrorCode
SyncTestBackend::Logv(char *fmt, va_list list)
{
   if (_logfp) {
      vfprintf(_logfp, fmt, list);

      constexpr int numBackTicks = 2;
      static char buffer[2048] = {'`', '`'};

      //vsnprintf(buffer+numBackTicks, 1024-numBackTicks, fmt, args); // why cant i use the safe version here?? only god knows
      snprintf(buffer+numBackTicks, 2048-numBackTicks, "%s", _logfp); // why cant i use the safe version here?? only god knows)
      ___GGPOlogSync(buffer);

   }
   return GGPO_OK;
}

void
SyncTestBackend::BeginLog(int saving)
{
   EndLog();

   char filename[MAX_PATH];
   CreateDirectoryA("synclogs", NULL);
   sprintf_s(filename, ARRAY_SIZE(filename), "synclogs\\%s-%04d-%s.log",
           saving ? "state" : "log",
           _sync.GetFrameCount(),
           _rollingback ? "replay" : "original");

    fopen_s(&_logfp, filename, "w");
}

void
SyncTestBackend::EndLog()
{
   if (_logfp) {
      fprintf(_logfp, "Closing log file.\n");
      fclose(_logfp);
      _logfp = NULL;
   }
}
void
SyncTestBackend::LogSaveStates(SavedInfo &info)
{

   // this file, bless its soul, was FUCKING CRASHING??
   // im hijacking it to just,,, pass both fucking save pointers in 
   // annnnd was the only reason for crashes in general. 
   // wow

   //Log("In LogSaveStates");

   //char filename[MAX_PATH];
   //sprintf_s(filename, ARRAY_SIZE(filename), "synclogs\\state-%04d-original.log", _sync.GetFrameCount());
   //_callbacks.log_game_state(filename, (unsigned char *)info.buf, info.cbuf);

   //sprintf_s(filename, ARRAY_SIZE(filename), "synclogs\\state-%04d-replay.log", _sync.GetFrameCount());
   //_callbacks.log_game_state(filename, _sync.GetLastSavedFrame().buf, _sync.GetLastSavedFrame().cbuf);

   Log("LogSaveStates called, and isnt doing anything bc,, ugh idek");
   //_callbacks.log_game_state((char*)_sync.GetLastSavedFrame().buf, (unsigned char *)info.buf, info.cbuf);

   //Log("left LogSaveStates");
}
