
#include <WS2tcpip.h>
#include <WinSock2.h>

#pragma comment(lib, "ws2_32.lib")


#include <stdio.h>

#define BUFFER_SIZE 1024

typedef struct _CLIENT_CONTEXT {
	SOCKET src, dst;
} CLIENT_CONTEXT, *PCLIENT_CONTEXT;


void usage(void)
{
	printf("Usage: bounce <src port> <dst address> <dst port>\n");
}

int forward(SOCKET src, SOCKET dst)
{
	int dataRead, dataWritten;
	char buf[BUFFER_SIZE];

	dataRead = recv(src, buf, BUFFER_SIZE, 0);
	if (dataRead == SOCKET_ERROR)
	{
		printf("recv() failed: %d\n", WSAGetLastError());
		return -1;
	}

	if (dataRead == 0)
		return 0;

	dataWritten = send(dst, buf, dataRead, 0);
	if (dataWritten == SOCKET_ERROR)
	{
		printf("send() failed: %d\n", WSAGetLastError());
		return -2;
	}

	return dataWritten;
}

DWORD
WINAPI
client_thread(LPVOID Parameter)
{
	PCLIENT_CONTEXT context = (PCLIENT_CONTEXT) Parameter;
	int err;
	FD_SET fds;

	for (;;)
	{
		FD_ZERO(&fds);
		FD_SET(context->src, &fds);
		FD_SET(context->dst, &fds);

		err = select(2, &fds, NULL, NULL, NULL);
		if (err == SOCKET_ERROR)
		{
			printf("select() failed: %d\n", WSAGetLastError());
			goto Cleanup;
		}

		if (FD_ISSET(context->src, &fds))
		{
			err = forward(context->src, context->dst);

			if (err == 0)
			{
				/* Connection gracefully closed */
				shutdown(context->src, SD_SEND);
				goto Cleanup;
			}
			else if (err == -1 || err == -2)
			{
				/* Socket failed */
				goto Cleanup;
			}
		}

		if (FD_ISSET(context->dst, &fds))
		{
			err = forward(context->dst, context->src);

			if (err == 0)
			{
				/* Connection gracefully closed */
				shutdown(context->dst, SD_SEND);
				goto Cleanup;
			}
			else if (err == -1 || err == -2)
			{
				/* Socket failed */
				goto Cleanup;
			}
		}
	}

Cleanup:
	closesocket(context->src);
	closesocket(context->dst);
	free(context);

	return 0;
}

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	int err;
	short srcPort;
	short dstPort;
	SOCKET server;
	SOCKADDR_IN bindAddr;
	SOCKADDR *connAddr;
	SIZE_T connAddrLen;
	ADDRINFO *addrInfo, hint;

	if (argc != 4)
	{
		usage();
		return -1;
	}

	srcPort = atoi(argv[1]);
	dstPort = atoi(argv[3]);
	if (srcPort == 0 || dstPort == 0)
	{
		usage();
		return -1;
	}

	err = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (err == SOCKET_ERROR)
	{
		printf("WSAStartup() failed: %d\n", WSAGetLastError());
		return -2;
	}


	RtlZeroMemory(&hint, sizeof(hint));
	hint.ai_family = AF_INET;
	hint.ai_protocol = IPPROTO_TCP;
	hint.ai_socktype = SOCK_STREAM;
	err = getaddrinfo(argv[2], NULL, &hint, &addrInfo);
	if (err == SOCKET_ERROR)
	{
		printf("getaddrinfo() failed: %d\n", WSAGetLastError());
		return -2;
	}

	connAddrLen = addrInfo->ai_addrlen;
	connAddr = (SOCKADDR*)malloc(connAddrLen);
	if (connAddr == NULL)
	{
		printf("malloc() failed\n");
		return -1;
	}

	RtlCopyMemory(connAddr, addrInfo->ai_addr, connAddrLen);
	((SOCKADDR_IN*)connAddr)->sin_port = htons(dstPort);

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server == INVALID_SOCKET)
	{
		printf("socket() failed: %d\n", WSAGetLastError());
		return -2;
	}

	RtlZeroMemory(&bindAddr, sizeof(bindAddr));
	bindAddr.sin_family = AF_INET;
	bindAddr.sin_port = htons(srcPort);
	bindAddr.sin_addr.S_un.S_addr = 0;
	err = bind(server, (SOCKADDR*)&bindAddr, sizeof(bindAddr));
	if (err == SOCKET_ERROR)
	{
		printf("bind() failed: %d\n", WSAGetLastError());
		return -2;
	}

	err = listen(server, SOMAXCONN);
	if (err == SOCKET_ERROR)
	{
		printf("listen() failed: %d\n", WSAGetLastError());
		return -2;
	}


	for (;;)
	{
		PCLIENT_CONTEXT context;
		HANDLE thread;
		
		context = (PCLIENT_CONTEXT) malloc(sizeof(*context));
		if (context == NULL)
		{
			printf("malloc() failed\n");
			return -2;
		}
				
		context->dst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (context->dst == INVALID_SOCKET)
		{
			printf("select() failed: %d\n", WSAGetLastError());
			return -2;
		}

		context->src = accept(server, NULL, NULL);
		if (context->src == INVALID_SOCKET)
		{
			printf("accept() failed: %d\n", WSAGetLastError());
			return -2;
		}

		err = connect(context->dst, connAddr, connAddrLen);
		if (err == SOCKET_ERROR)
		{
			printf("connect() failed: %d\n", WSAGetLastError());
			return -2;
		}

		thread = CreateThread(NULL, 0, client_thread, (LPVOID)context, 0, NULL);
		if (thread == INVALID_HANDLE_VALUE)
		{
			printf("CreateThread() failed: %d\n", GetLastError());
			return -2;
		}

		CloseHandle(thread);
	}

	return 0;
}

