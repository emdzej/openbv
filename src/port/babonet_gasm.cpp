/*
	openbv: baboNet (Engine/babonet, the game's network DLL) on gasm.

	The original spoke its own framing over TCP (and UDP) sockets. gasm guests have WebSockets only
	(gasm:net), so here:

	- a client connects to ws://<host>:<port>/ (or to a ws:// or wss:// URL given as the host), and
	  every baboNet packet is one binary message: u16 type ID (little-endian), u8 protocol (0 = TCP,
	  1 = UDP, as the game asked), u8 0, then the packet's bytes. This is what the openbv server
	  (server/) speaks.
	- a server can't listen in a browser, so bb_serverCreate makes an in-process server that the game's
	  own client reaches by connecting to the local address and the server's port (a hosted game, with
	  the server running in the same module, as the original did in the same process).
	- the peer-to-peer UDP part (LAN broadcasts, server pings, remote admin) has no transport: sends go
	  nowhere and nothing is received.

	What the game sees keeps the original's rules: bb_serverUpdate reports one event per call (a new
	client's ID, a lost client's ID negated), new connections are accepted at its half-second checks,
	packets come out oldest first, and a received packet stays valid until the next receive on the
	same side.

	This file is part of openbv, under the GNU General Public License v3 or later.
*/

#include "baboNet.h"
#include "gasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <deque>
#include <vector>
#include <string>

namespace
{
	char version[] = "4.0";
	char myIP[] = "127.0.0.1";

	struct Packet
	{
		int type;
		std::vector<char> data;
	};

	struct Queue
	{
		std::deque<Packet> packets;
		Packet current;           // the one handed out last, alive until the next receive
		char *take(int *type, int *size)
		{
			if (packets.empty()) return 0;
			current = packets.front();
			packets.pop_front();
			*type = current.type;
			if (size) *size = (int)current.data.size();
			return current.data.empty() ? (char *)1 : &current.data[0];
		}
	};

	struct LocalServer;

	struct Client
	{
		UINT4 id;
		bool loopback;
		int ws;                   // gasm:net handle (remote)
		int state;                // 0 connecting, 1 connected, 2 closed by the server, 3 failed
		bool reported;            // connected/failed reported once by bb_clientUpdate
		UINT4 serverSideID;       // loopback: our ID on the local server
		Queue in;
		UINT4 bytesSent, bytesReceived;
		char lastError[256];
		char lastMessage[256];
	};

	struct ServerClient
	{
		UINT4 netID;
		Client *loop;             // the loopback client on the other end
		Queue in;
		bool gone;
	};

	struct LocalServer
	{
		unsigned short port;
		int maxClients;
		float connCheck;
		UINT4 lastNetID;
		std::vector<ServerClient *> clients;
		std::deque<Client *> pending;
		std::deque<INT4> lostEvents;
		UINT4 bytesSent, bytesReceived;
		unsigned char flags;
		char lastError[256];
		char lastMessage[256];
	};

	std::vector<Client *> clients;
	UINT4 lastClientID = 0;
	LocalServer *server = 0;
	char peerError[256] = "";
	char peerMessage[256] = "";

	Client *clientByID(UINT4 id)
	{
		for (size_t i = 0; i < clients.size(); i++) if (clients[i]->id == id) return clients[i];
		return 0;
	}

	ServerClient *serverClientByID(UINT4 netID)
	{
		if (!server) return 0;
		for (size_t i = 0; i < server->clients.size(); i++)
			if (server->clients[i]->netID == netID && !server->clients[i]->gone) return server->clients[i];
		return 0;
	}

	bool isLocalHost(const char *host)
	{
		return !*host || strcmp(host, "127.0.0.1") == 0 || strcasecmp(host, "localhost") == 0 || strcmp(host, myIP) == 0;
	}

	void wsSend(Client *c, const char *data, int size, int type, int protocol)
	{
		std::vector<unsigned char> msg(4 + (size > 0 ? size : 0));
		msg[0] = (unsigned char)(type & 0xff);
		msg[1] = (unsigned char)((type >> 8) & 0xff);
		msg[2] = (unsigned char)(protocol ? 1 : 0);
		msg[3] = 0;
		if (size > 0 && data) memcpy(&msg[4], data, size);
		if (gasm_net_send(c->ws, &msg[0], (uint32_t)msg.size()) == 0) c->bytesSent += (UINT4)msg.size();
	}

	// Pull everything gasm:net has for a remote client into its queue.
	void wsPump(Client *c)
	{
		if (c->loopback || c->ws <= 0) return;
		uint32_t st = gasm_net_state(c->ws);
		if (c->state == 0)
		{
			if (st == 1) c->state = 1;
			else if (st >= 2)
			{
				c->state = 3;
				snprintf(c->lastError, sizeof(c->lastError), "Error : connection failed");
			}
		}
		static std::vector<unsigned char> buf(65536 + 4);
		for (;;)
		{
			int32_t n = gasm_net_recv(c->ws, &buf[0], (uint32_t)buf.size());
			if (n == 0) break;
			if (n < 0)
			{
				if (c->state == 1) c->state = 2;
				break;
			}
			if (n > (int32_t)buf.size()) { buf.resize(n); continue; }
			c->bytesReceived += (UINT4)n;
			if (n < 4) continue;
			Packet p;
			p.type = buf[0] | (buf[1] << 8);
			p.data.assign((char *)&buf[4], (char *)&buf[0] + n);
			c->in.packets.push_back(p);
		}
		if (c->state == 1 && st >= 2 && c->in.packets.empty()) c->state = 2;
	}

	// --param netlog=1: every packet the game's client sends and receives, on stdout, to compare
	// servers (design/server.md §10.3). Off unless asked for; nothing else changes.
	int netlog = -1;
	void logPacket(const char *dir, int type, const char *data, int size)
	{
		if (netlog < 0)
		{
			char v[8] = "";
			gasm_param_str("netlog", v, sizeof(v));
			netlog = (v[0] == '1') ? 1 : 0;
		}
		if (!netlog) return;
		printf("netlog %s type=%d size=%d data=", dir, type, size);
		for (int i = 0; i < size && data && data != (const char *)1; i++) printf("%02x", (unsigned char)data[i]);
		printf("\n");
	}

	void dropServerClient(ServerClient *sc)
	{
		if (sc->gone) return;
		sc->gone = true;
		if (sc->loop) { sc->loop->state = 2; sc->loop->serverSideID = 0; }
	}
}

// --- startup

int bb_init() { return 0; }
char *bb_getVersion() { return version; }
char *bb_getMyIP() { return myIP; }

// A made-up MAC, the same on every run of this installation (players are banned by it).
void bb_getMyMAC(unsigned char *AddrOut)
{
	static unsigned char mac[6];
	static bool have = false;
	if (!have)
	{
		const char key[] = "babonet-mac";
		if (gasm_storage_get(key, sizeof(key) - 1, mac, 6) != 6)
		{
			for (int i = 0; i < 6; i++) mac[i] = (unsigned char)(rand() & 0xff);
			mac[0] = (mac[0] & 0xfe) | 0x02;   // locally administered, unicast
			gasm_storage_set(key, sizeof(key) - 1, mac, 6);
		}
		have = true;
	}
	memcpy(AddrOut, mac, 6);
}

void bb_enable(unsigned char netFlags) { if (server) server->flags |= netFlags; }
void bb_disable(unsigned char netFlags) { if (server) server->flags &= ~netFlags; }

void bb_shutdown()
{
	bb_serverShutdown();
	for (size_t i = 0; i < clients.size(); i++)
	{
		if (!clients[i]->loopback && clients[i]->ws > 0) gasm_net_close(clients[i]->ws);
		delete clients[i];
	}
	clients.clear();
}

// --- the in-process server

int bb_serverCreate(bool UDPenabled, int maxClients, unsigned short listenPort)
{
	(void)UDPenabled;
	if (server) bb_serverShutdown();
	if (listenPort == 11111) return 1;   // reserved by baboNet, as in the original
	server = new LocalServer();
	server->port = listenPort;
	server->maxClients = maxClients;
	server->connCheck = 0;
	server->lastNetID = 0;
	server->bytesSent = server->bytesReceived = 0;
	server->flags = NET_ACCEPT_CLIENTS;
	server->lastError[0] = server->lastMessage[0] = 0;
	return 0;
}

INT4 bb_serverUpdate(float elapsed, int updateMsg, char *NewIP)
{
	if (!server) return BBNET_ERROR;
	server->lastError[0] = server->lastMessage[0] = 0;
	if (updateMsg == UPDATE_SEND) return 0;
	if (!server->lostEvents.empty())
	{
		INT4 lost = server->lostEvents.front();
		server->lostEvents.pop_front();
		return lost;
	}
	server->connCheck += elapsed;
	if (server->connCheck >= 0.5f)
	{
		server->connCheck = 0;
		while (!server->pending.empty())
		{
			Client *c = server->pending.front();
			server->pending.pop_front();
			if (c->state != 0) continue;
			int live = 0;
			for (size_t i = 0; i < server->clients.size(); i++) if (!server->clients[i]->gone) live++;
			if (!(server->flags & NET_ACCEPT_CLIENTS) || live >= server->maxClients)
			{
				c->state = 3;
				snprintf(c->lastError, sizeof(c->lastError), "Error : server is full");
				continue;
			}
			ServerClient *sc = new ServerClient();
			sc->netID = ++server->lastNetID;
			sc->loop = c;
			sc->gone = false;
			server->clients.push_back(sc);
			c->serverSideID = sc->netID;
			c->state = 1;
			if (NewIP) strcpy(NewIP, myIP);
			return (INT4)sc->netID;
		}
	}
	return 0;
}

int bb_serverSend(char *dataToSend, int dataSize, int typeID, INT4 destination, int protocol)
{
	(void)protocol;
	if (!server) return 1;
	Packet p;
	p.type = typeID;
	if (dataSize > 0 && dataToSend) p.data.assign(dataToSend, dataToSend + dataSize);
	if (destination < 1)
	{
		for (size_t i = 0; i < server->clients.size(); i++)
		{
			ServerClient *sc = server->clients[i];
			if (sc->gone || !sc->loop) continue;
			sc->loop->in.packets.push_back(p);
			server->bytesSent += (UINT4)dataSize;
		}
		return 0;
	}
	ServerClient *sc = serverClientByID((UINT4)destination);
	if (!sc)
	{
		snprintf(server->lastError, sizeof(server->lastError), "Error : Invalid baboNet ID for Destination, packet was NOT sent");
		return 1;
	}
	if (sc->loop) sc->loop->in.packets.push_back(p);
	server->bytesSent += (UINT4)dataSize;
	return 0;
}

char *bb_serverReceive(UINT4 &babonetID, int &typeID, int *size)
{
	if (!server) return 0;
	for (size_t i = 0; i < server->clients.size(); i++)
	{
		ServerClient *sc = server->clients[i];
		if (sc->gone) continue;
		char *d = sc->in.take(&typeID, size);
		if (d) { babonetID = sc->netID; return d; }
	}
	return 0;
}

char *bb_serverGetLastError() { return server ? server->lastError : 0; }
char *bb_serverGetLastMessage() { return server ? server->lastMessage : 0; }
int bb_serverDisconnectClient(UINT4 baboNetID)
{
	ServerClient *sc = serverClientByID(baboNetID);
	if (!sc) return 1;
	dropServerClient(sc);
	return 0;
}

int bb_serverShutdown()
{
	if (!server) return 0;
	for (size_t i = 0; i < server->clients.size(); i++)
	{
		dropServerClient(server->clients[i]);
		delete server->clients[i];
	}
	for (size_t i = 0; i < server->pending.size(); i++) server->pending[i]->state = 3;
	delete server;
	server = 0;
	return 0;
}

int bb_serverGetQueueCount(int protocol) { (void)protocol; return 0; }
UINT4 bb_serverGetBytesSent() { return server ? server->bytesSent : 0; }
UINT4 bb_serverGetBytesReceived() { return server ? server->bytesReceived : 0; }
int bb_serverSetClientRate(int nbBytes, UINT4 baboNetID) { (void)nbBytes; return serverClientByID(baboNetID) ? 0 : 1; }

// --- clients

UINT4 bb_clientConnect(const char *HostIP, unsigned short Port)
{
	Client *c = new Client();
	c->id = ++lastClientID;
	c->ws = 0;
	c->state = 0;
	c->reported = false;
	c->serverSideID = 0;
	c->bytesSent = c->bytesReceived = 0;
	c->lastError[0] = c->lastMessage[0] = 0;
	c->loopback = server && Port == server->port && isLocalHost(HostIP);
	if (c->loopback)
	{
		server->pending.push_back(c);
	}
	else if (!*HostIP)
	{
		c->state = 3;   // no address (no master server configured): fail without trying
		snprintf(c->lastError, sizeof(c->lastError), "Error : no address");
	}
	else
	{
		char url[512];
		if (strncmp(HostIP, "ws://", 5) == 0 || strncmp(HostIP, "wss://", 6) == 0) snprintf(url, sizeof(url), "%s", HostIP);
		else snprintf(url, sizeof(url), "ws://%s:%u/", HostIP, (unsigned)Port);
		c->ws = gasm_net_open(url, (uint32_t)strlen(url));
		if (c->ws <= 0)
		{
			c->state = 3;
			snprintf(c->lastError, sizeof(c->lastError), "Error : can not connect to %s", url);
		}
	}
	clients.push_back(c);
	return c->id;
}

int bb_clientUpdate(UINT4 clientID, float elapsed, int updateMsg)
{
	(void)elapsed; (void)updateMsg;
	Client *c = clientByID(clientID);
	if (!c) return 1;
	c->lastError[0] = c->lastMessage[0] = 0;
	wsPump(c);
	if (!c->reported)
	{
		if (c->state == 1) { c->reported = true; return CLIENT_CONNECT_SUCCESS; }
		if (c->state >= 2) { c->reported = true; return CLIENT_CONNECT_FAILURE; }
		return 0;
	}
	if (c->state == 2 && c->in.packets.empty()) return SERVER_CLOSED_CONNECTION;
	if (c->state == 3) return CLIENT_CONNECT_FAILURE;
	return 0;
}

int bb_clientSend(UINT4 clientID, char *dataToSend, int dataSize, int typeID, int protocol)
{
	Client *c = clientByID(clientID);
	if (!c) return 1;
	if (c->state != 1) return 0;
	logPacket("send", typeID, dataToSend, dataSize);
	if (c->loopback)
	{
		ServerClient *sc = serverClientByID(c->serverSideID);
		if (!sc) return 0;
		Packet p;
		p.type = typeID;
		if (dataSize > 0 && dataToSend) p.data.assign(dataToSend, dataToSend + dataSize);
		sc->in.packets.push_back(p);
		c->bytesSent += (UINT4)dataSize;
		if (server) server->bytesReceived += (UINT4)dataSize;
		return 0;
	}
	wsSend(c, dataToSend, dataSize, typeID, protocol);
	return 0;
}

char *bb_clientReceive(UINT4 clientID, int *typeID)
{
	Client *c = clientByID(clientID);
	if (!c) { *typeID = 0; return 0; }
	int size = 0;
	char *d = c->in.take(typeID, &size);
	if (d) logPacket("recv", *typeID, d, size);
	return d;
}

int bb_clientDisconnect(UINT4 clientID)
{
	for (size_t i = 0; i < clients.size(); i++)
	{
		Client *c = clients[i];
		if (c->id != clientID) continue;
		if (c->loopback)
		{
			ServerClient *sc = serverClientByID(c->serverSideID);
			if (sc)
			{
				sc->gone = true;
				sc->loop = 0;
				server->lostEvents.push_back(-(INT4)sc->netID);
			}
			if (server)
				for (size_t k = 0; k < server->pending.size(); k++)
					if (server->pending[k] == c) { server->pending.erase(server->pending.begin() + k); break; }
		}
		else if (c->ws > 0) gasm_net_close(c->ws);
		delete c;
		clients.erase(clients.begin() + i);
		return 0;
	}
	return 1;
}

static char clientMissing[256] = "Error : Client is not availble";
char *bb_clientGetLastError(UINT4 clientID) { Client *c = clientByID(clientID); return c ? c->lastError : clientMissing; }
char *bb_clientGetLastMessage(UINT4 clientID) { Client *c = clientByID(clientID); return c ? c->lastMessage : 0; }
UINT4 bb_clientGetBytesSent(UINT4 clientID) { Client *c = clientByID(clientID); return c ? c->bytesSent : 0; }
UINT4 bb_clientGetBytesReceived(UINT4 clientID) { Client *c = clientByID(clientID); return c ? c->bytesReceived : 0; }
int bb_clientSetRate(UINT4 clientID, int nbBytes) { (void)nbBytes; return clientByID(clientID) ? 0 : 1; }

// --- peer to peer: no transport on gasm

int bb_peerUpdate(float elapsed, bool &isNew) { (void)elapsed; isNew = false; return 0; }
int bb_peerBindPort(unsigned short listenPort) { (void)listenPort; return 0; }
int bb_peerSend(INT4 peerID, char *data, int typeID, int dataSize, bool safe) { (void)peerID; (void)data; (void)typeID; (void)dataSize; (void)safe; return 0; }
int bb_peerSend(char *IPdomain, unsigned short port, char *data, int typeID, int dataSize, bool safe)
{
	(void)IPdomain; (void)port; (void)data; (void)typeID; (void)dataSize; (void)safe;
	static INT4 lastPeer = 0;
	return ++lastPeer;
}
char *bb_peerReceive(INT4 *fromPeerID, int *typeID) { (void)fromPeerID; (void)typeID; return 0; }
int bb_peerGetIPport(UINT4 babonetID, char *IP, unsigned short *Port) { (void)babonetID; if (IP) IP[0] = 0; if (Port) *Port = 0; return 1; }
int bb_peerDelete(UINT4 baboNetID, bool instant) { (void)baboNetID; (void)instant; return 0; }
int bb_peerShutdown() { return 0; }
char *bb_peerGetLastError() { return peerError; }
char *bb_peerGetLastMessage() { return peerMessage; }
UINT4 bb_peerGetBytesSent() { return 0; }
UINT4 bb_peerGetBytesReceived() { return 0; }
