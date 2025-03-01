/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include "types.h"

void ___log(const char* msg)
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

static FILE *logfile = NULL;

void LogFlush()
{
   if (logfile) {
      fflush(logfile);
   }
}

static char logbuf[4 * 1024 * 1024];

void Log(const char *fmt, ...)
{
   //va_list args;
   //va_start(args, fmt);
   //Logv(fmt, args);
   //va_end(args);

   constexpr int numBackTicks = 5;
	static char buffer[2048] = {'`', '`', '`', '`', '`'};
	va_list args;
	va_start(args, fmt);
	//vsnprintf(buffer+numBackTicks, 1024-numBackTicks, fmt, args); // why cant i use the safe version here?? only god knows
	vsprintf_s(buffer+numBackTicks, 1024-numBackTicks, fmt, args); // why cant i use the safe version here?? only god knows)
   ___log(buffer);
	va_end(args);
}

void Logv(const char *fmt, va_list args)
{
   if (!Platform::GetConfigBool("ggpo.log") || Platform::GetConfigBool("ggpo.log.ignore")) {
      return;
   }
   if (!logfile) {
      sprintf_s(logbuf, ARRAY_SIZE(logbuf), "log-%d.log", Platform::GetProcessID());
      fopen_s(&logfile, logbuf, "w");
   }
   Logv(logfile, fmt, args);
}

void Logv(FILE *fp, const char *fmt, va_list args)
{
   if (Platform::GetConfigBool("ggpo.log.timestamps")) {
      static int start = 0;
      int t = 0;
      if (!start) {
         start = Platform::GetCurrentTimeMS();
      } else {
         t = Platform::GetCurrentTimeMS() - start;
      }
      fprintf(fp, "%d.%03d : ", t / 1000, t % 1000);
   }

   vfprintf(fp, fmt, args);
   fflush(fp);
   
   vsprintf_s(logbuf, ARRAY_SIZE(logbuf), fmt, args);
}

