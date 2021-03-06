// 2018GameserverTerm.cpp: 콘솔 응용 프로그램의 진입점을 정의합니다.
//

#include "stdafx.h"

HANDLE g_iocp;

static const char EVT_RECV = 0;
static const char EVT_SEND = 1;
static const char EVT_PLAYER_MOVE = 2;
static const char EVT_MOVE = 3;

static const int E_MOVE = 0;
static const int E_ATTACK = 1;
static const int E_HEAL = 2;
static const int E_PLAYER_MOVE = 3;


struct EXOver {
	WSAOVERLAPPED wsaover;
	char  event_type;
	int event_target;
	WSABUF wsabuf;
	char io_buf[MAX_BUFF_SIZE];
};

struct CLIENT {
	SOCKET s;
	bool in_use;
	short x, y;
	unordered_set <int> viewPlayerlist;
	unordered_set <int> viewNPClist;
	mutex vlm;//npc용이랑 분리해야

	bool is_active;
	//lua_State *L;

	EXOver exover;
	int packet_size;
	int prev_size;
	char prev_packet[MAX_PACKET_SIZE];
};

struct NPC {
	short x, y;
	unordered_set <int> viewlist;
	mutex vlm;
	bool is_active;
	unsigned int last_tick_time;
};

CLIENT g_clients[MAX_USER];

NPC g_npcs[NUM_OF_NPC];

struct EVENT {
	unsigned int s_time;
	int type;
	int object;
	int target;
};

class mycomparison
{
	bool reverse;
public:
	mycomparison() {}
	bool operator() (const EVENT lhs, const EVENT rhs) const
	{
		return (lhs.s_time > rhs.s_time);
	}
};

priority_queue < EVENT, vector<EVENT>, mycomparison> timer_queue;

void add_timer(int id, int type, unsigned int s_time)
{
	timer_queue.push(EVENT{ s_time, type, id, 0 });
}

void timer_thread()
{
	while (true) {
		Sleep(10);
		while (false == timer_queue.empty()) {
			if (timer_queue.top().s_time > GetTickCount()) break;
			EVENT ev = timer_queue.top();
			timer_queue.pop();
			EXOver *ex = new EXOver;
			ex->event_type = EVT_MOVE;
			PostQueuedCompletionStatus(g_iocp, 1, ev.object, &ex->wsaover);
		}
	}
}

void error_display(const char *msg, int err_no)
{
	WCHAR *lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 " << lpMsgBuf << endl;
	LocalFree(lpMsgBuf);
}

bool CanSeePlayer(int a, int b)
{
	int dist_sq = (g_clients[a].x - g_clients[b].x) *(g_clients[a].x - g_clients[b].x)
		+ (g_clients[a].y - g_clients[b].y) *(g_clients[a].y - g_clients[b].y);
	return (dist_sq <= VIEW_RADIUS * VIEW_RADIUS);
}

bool CanSeeNPC(int a, int b)
{
	int dist_sq = (g_clients[a].x - g_npcs[b].x) *(g_clients[a].x - g_npcs[b].x)
		+ (g_clients[a].y - g_npcs[b].y) *(g_clients[a].y - g_npcs[b].y);
	return (dist_sq <= VIEW_RADIUS * VIEW_RADIUS);
}

void Initialize()
{
	wcout.imbue(locale("korean"));
	wcout << L"한글 메세지 출력!\n";

	for (auto &cl : g_clients) {
		cl.in_use = false;
		cl.x = rand() % BOARD_WIDTH;
		cl.y = rand() % BOARD_HEIGHT;
		cl.is_active = false;
		cl.exover.event_type = EVT_RECV;
		cl.exover.wsabuf.buf = cl.exover.io_buf;
		cl.exover.wsabuf.len = sizeof(cl.exover.io_buf);
		cl.packet_size = 0;
		cl.prev_size = 0;
	}

	for (auto&cl : g_npcs)
	{
		cl.is_active = false;
		cl.x = rand() % BOARD_WIDTH;
		cl.y = rand() % BOARD_HEIGHT;
	}

	//for (int i = NPC_START; i < NUM_OF_NPC; ++i) {
	//	lua_State *L = luaL_newstate();
	//	luaL_openlibs(L);
	//	int error = luaL_loadfile(L, "monster.lua");
	//	lua_pcall(L, 0, 0, 0);
	//	lua_getglobal(L, "set_myid");
	//	lua_pushnumber(L, i);
	//	lua_pcall(L, 1, 0, 0);
	//	lua_register(L, "API_send_message", CAPI_send_message);
	//	lua_register(L, "API_get_x", CAPI_get_x);
	//	lua_register(L, "API_get_y", CAPI_get_y);

	//	g_clients[i].L = L;
	//}
	//cout << "NPC Initializing finished!\n";

	WSADATA	wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, 0, 0, 0);
}

void SendPacket(int cl, void *packet)
{
	EXOver *o = new EXOver;
	unsigned char *p = reinterpret_cast<unsigned char *>(packet);
	memcpy(o->io_buf, p, p[0]);
	o->event_type = EVT_SEND;
	o->wsabuf.buf = o->io_buf;
	o->wsabuf.len = p[0];
	ZeroMemory(&o->wsaover, sizeof(WSAOVERLAPPED));

	int ret = WSASend(g_clients[cl].s, &o->wsabuf, 1, NULL, 0, &o->wsaover, NULL);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no)
			error_display("Error in SendPacket:", err_no);
	}

	cout << "SendPacket to Client [" << cl << "] Type ["
		<< (int)p[1] << "] Size [" << (int)p[0] << "]\n";
}

void SendPutPlayer(int client, int object_id)
{
	sc_packet_put_player p;
	p.id = object_id;
	p.size = sizeof(p);
	p.type = SC_PUT_PLAYER;
	p.x = g_clients[object_id].x;
	p.y = g_clients[object_id].y;

	SendPacket(client, &p);
}

void SendPutObject(int client, int object_id)
{
	sc_packet_put_player p;
	p.id = object_id;
	p.size = sizeof(p);
	p.type = SC_PUT_OBJECT;
	p.x = g_npcs[object_id].x;
	p.y = g_npcs[object_id].y;

	SendPacket(client, &p);
}

void WakeUpNPC(int id)
{
	if (true == g_npcs[id].is_active) return;
	g_npcs[id].is_active = true;
	add_timer(id, E_MOVE, GetTickCount() + 1000);
}

void DisconnectPlayer(int cl)
{
	closesocket(g_clients[cl].s);
	cout << "CLient [" << cl << "] Disconnected.\n";

	sc_packet_remove_player p;
	p.id = cl;
	p.size = sizeof(p);
	p.type = SC_REMOVE_PLAYER;
	g_clients[cl].vlm.lock();
	unordered_set <int> vl_copy = g_clients[cl].viewPlayerlist;
	g_clients[cl].viewPlayerlist.clear();
	g_clients[cl].vlm.unlock();

	for (int id : vl_copy) {
		g_clients[id].vlm.lock();
		if (true == g_clients[id].in_use) {
			if (0 != g_clients[id].viewPlayerlist.count(cl)) {
				g_clients[id].viewPlayerlist.erase(cl);
				g_clients[id].vlm.unlock();
				SendPacket(id, &p);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
		else {
			g_clients[id].vlm.unlock();
		}
	}
	g_clients[cl].vlm.lock();
	g_clients[cl].viewNPClist.clear();
	g_clients[cl].vlm.unlock();
	g_clients[cl].in_use = false;
}

void SendRemovePlayer(int client, int object_id)
{
	sc_packet_remove_player p;
	p.id = object_id;
	p.size = sizeof(p);
	p.type = SC_REMOVE_PLAYER;

	SendPacket(client, &p);
}

void SendRemoveObject(int client, int object_id)
{
	sc_packet_remove_player p;
	p.id = object_id;
	p.size = sizeof(p);
	p.type = SC_REMOVE_OBJECT;

	SendPacket(client, &p);
}

void ProcessPacket(int cl, char *packet)
{
	cs_packet_up *p = reinterpret_cast<cs_packet_up *>(packet);

	switch (p->type) {
	case CS_UP:
		g_clients[cl].y--;
		if (0 > g_clients[cl].y) g_clients[cl].y = 0;
		break;
	case CS_DOWN:
		g_clients[cl].y++;
		if (BOARD_HEIGHT <= g_clients[cl].y) g_clients[cl].y = BOARD_HEIGHT - 1;
		break;
	case CS_LEFT:
		g_clients[cl].x--;
		if (0 > g_clients[cl].x) g_clients[cl].x = 0;
		break;
	case CS_RIGHT:
		g_clients[cl].x++;
		if (BOARD_WIDTH <= g_clients[cl].x) g_clients[cl].x = BOARD_WIDTH - 1;
		break;
	default: cout << "Unknown Protocl from Client[" << cl << "]\n";
		return;
	}

	sc_packet_pos sp;
	sp.id = cl;
	sp.size = sizeof(sc_packet_pos);
	sp.type = SC_POS;
	sp.x = g_clients[cl].x;
	sp.y = g_clients[cl].y;

	unordered_set <int> new_view_list;
	unordered_set <int> new_view_npc_list;
	for (int i = 0; i < MAX_USER; ++i) {
		if (i == cl) continue;
		if (false == g_clients[i].in_use) continue;
		if (false == CanSeePlayer(cl, i)) continue;
		new_view_list.insert(i);
	}
	for (int i = 0; i < NUM_OF_NPC; ++i) {
		if (false == CanSeeNPC(cl, i)) continue;
		new_view_npc_list.insert(i);
		// 시야내에 있는 플레이어가 이동했다는 이벤트 발생
		EXOver *exover = new EXOver;
		exover->event_type = EVT_PLAYER_MOVE;
		exover->event_target = cl;
		PostQueuedCompletionStatus(g_iocp, 1, i, &exover->wsaover);
	}

	SendPacket(cl, &sp);

	// 새로 Viewlist에 들어오는 객체 처리
	for (auto id : new_view_list) {
		g_clients[cl].vlm.lock();
		if (0 == g_clients[cl].viewPlayerlist.count(id)) {
			g_clients[cl].viewPlayerlist.insert(id);
			g_clients[cl].vlm.unlock();
			SendPutPlayer(cl, id);
			g_clients[id].vlm.lock();
			if (0 == g_clients[id].viewPlayerlist.count(cl)) {
				g_clients[id].viewPlayerlist.insert(cl);
				g_clients[id].vlm.unlock();
				SendPutPlayer(id, cl);
			}
			else {
				g_clients[id].vlm.unlock();
				SendPacket(id, &sp);
			}
		}
		else {
			g_clients[cl].vlm.unlock();
			// Viewlist에 계속 남아있는 객체 처리
			g_clients[id].vlm.lock();
			if (0 == g_clients[id].viewPlayerlist.count(cl)) {
				g_clients[id].viewPlayerlist.insert(cl);
				g_clients[id].vlm.unlock();
				SendPutPlayer(id, cl);
			}
			else {
				g_clients[id].vlm.unlock();
				SendPacket(id, &sp);
			}
		}
	}

	for (auto id : new_view_npc_list)
	{
		g_clients[cl].vlm.lock();
		WakeUpNPC(id);
		SendPutObject(cl, id);
		g_clients[cl].vlm.unlock();
	}

	// Viewlist에서 나가는 객체 처리
	g_clients[cl].vlm.lock();
	unordered_set <int> old_vl = g_clients[cl].viewPlayerlist;
	g_clients[cl].vlm.unlock();
	for (auto id : old_vl) {
		if (0 == new_view_list.count(id)) {
			g_clients[cl].vlm.lock();
			g_clients[cl].viewPlayerlist.erase(id);
			g_clients[cl].vlm.unlock();
			SendRemovePlayer(cl, id);

			g_clients[id].vlm.lock();
			if (0 != g_clients[id].viewPlayerlist.count(cl)) {
				g_clients[id].viewPlayerlist.erase(cl);
				g_clients[id].vlm.unlock();
				SendRemovePlayer(id, cl);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
	}

	g_clients[cl].vlm.lock();
	unordered_set <int> old_vln = g_clients[cl].viewNPClist;
	g_clients[cl].vlm.unlock();
	for (auto id : old_vln)
	{
		if (0 == new_view_npc_list.count(id))
		{
			SendRemoveObject(cl, id);
		}
	}
}

void MoveNPC(int i)
{
	unordered_set <int> old_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSeeNPC(id, i)) {
				old_vl.insert(id);
			}

	switch (rand() % 4) {
	case 0: if (g_clients[i].y > 0) g_npcs[i].y--; break;
	case 1: if (g_clients[i].y < BOARD_HEIGHT - 1) g_npcs[i].y++; break;
	case 2: if (g_clients[i].x > 0) g_npcs[i].x--; break;
	case 3: if (g_clients[i].x < BOARD_WIDTH - 1) g_npcs[i].x++; break;
	default: break;
	}

	volatile int k = 0;
	for (int j = 0; j < 10000; ++j) k = k + j;

	unordered_set<int> new_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSeeNPC(id, i)) 
				new_vl.insert(id);

	sc_packet_pos p_packet;
	p_packet.id = i;
	p_packet.size = sizeof(p_packet);
	p_packet.type = SC_POS_OBJECT;
	p_packet.x = g_clients[i].x;
	p_packet.y = g_clients[i].y;

	// PutObject
	for (auto id : new_vl) {
		g_clients[id].vlm.lock();
		if (0 == g_clients[id].viewNPClist.count(i)) {
			g_clients[id].viewNPClist.insert(i);
			g_clients[id].vlm.unlock();
			SendPutObject(id, i);
		}
		else {
			g_clients[id].vlm.unlock();
			SendPacket(id, &p_packet);
		}
	}
	// RemoveObject
	for (auto id : old_vl) {
		if (0 == new_vl.count(id)) {
			g_clients[id].vlm.lock();
			if (0 != g_clients[id].viewNPClist.count(i)) {
				g_clients[id].viewNPClist.erase(i);
				g_clients[id].vlm.unlock();
				SendRemoveObject(id, i);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
	}

	if (true != new_vl.empty()) {
		add_timer(i, E_MOVE, GetTickCount() + 1000);
	}
}

void WorkerThread()
{
	while (true) {
		unsigned long data_size;
		unsigned long long key;
		WSAOVERLAPPED *p_over;

		BOOL is_success = GetQueuedCompletionStatus(g_iocp,
			&data_size, &key, &p_over, INFINITE);
		cout << "GQCS from client [" << key << "] with size [" << data_size << "]\n";
		// 에러 처리
		if (0 == is_success) {
			cout << "Error in GQCS key[" << key << "]\n";
			DisconnectPlayer(key);
			continue;
		}
		// 접속종료 처리
		if (0 == data_size) {
			DisconnectPlayer(key);
			continue;
		}
		// Send/Recv 처리
		EXOver *o = reinterpret_cast<EXOver *>(p_over);
		if (EVT_RECV == o->event_type) {
			int r_size = data_size;
			char *ptr = o->io_buf;
			while (0 < r_size) {
				if (0 == g_clients[key].packet_size)
					g_clients[key].packet_size = ptr[0];
				int remain = g_clients[key].packet_size - g_clients[key].prev_size;
				if (remain <= r_size) {
					memcpy(g_clients[key].prev_packet + g_clients[key].prev_size,
						ptr, remain);
					ProcessPacket(static_cast<int>(key), g_clients[key].prev_packet);
					r_size -= remain;
					ptr += remain;
					g_clients[key].packet_size = 0;
					g_clients[key].prev_size = 0;
				}
				else {
					memcpy(g_clients[key].prev_packet + g_clients[key].prev_size,
						ptr,
						r_size);
					g_clients[key].prev_size += r_size;
					r_size -= r_size;
					ptr += r_size;
				}
			}
			unsigned long  rflag = 0;
			ZeroMemory(&o->wsaover, sizeof(WSAOVERLAPPED));
			WSARecv(g_clients[key].s, &o->wsabuf, 1, NULL,
				&rflag, &o->wsaover, NULL);
		}
		else if (EVT_SEND == o->event_type) {
			delete o;
		}
		else if (EVT_MOVE == o->event_type) {
			MoveNPC(key);
		}
		/*else if (EVT_PLAYER_MOVE == o->event_type) {
			EXOver *o = reinterpret_cast<EXOver *>(p_over);
			int player = o->event_target;
			lua_getglobal(g_clients[key].L, "event_player_move");
			lua_pushnumber(g_clients[key].L, player);
			lua_pcall(g_clients[key].L, 1, 0, 0);
			delete o;
		}
		*/else
			cout << "Unknown Event Error in worker thread!\n";
	}
}

void AcceptThread()
{
	auto g_socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);

	SOCKADDR_IN bind_addr;
	ZeroMemory(&bind_addr, sizeof(SOCKADDR_IN));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(MY_SERVER_PORT);
	bind_addr.sin_addr.s_addr = INADDR_ANY;

	::bind(g_socket, reinterpret_cast<sockaddr *>(&bind_addr),
		sizeof(SOCKADDR_IN));
	listen(g_socket, 1000);
	while (true) {
		SOCKADDR_IN c_addr;
		ZeroMemory(&c_addr, sizeof(SOCKADDR_IN));
		c_addr.sin_family = AF_INET;
		c_addr.sin_port = htons(MY_SERVER_PORT);
		c_addr.sin_addr.s_addr = INADDR_ANY;
		int c_length = sizeof(SOCKADDR_IN);

		auto new_socket = WSAAccept(g_socket,
			reinterpret_cast<sockaddr *>(&c_addr),
			&c_length, NULL, NULL);
		cout << "New Client Accepted\n";
		int new_key = -1;
		for (int i = 0; i<MAX_USER; ++i)
			if (false == g_clients[i].in_use) {
				new_key = i;
				break;
			}
		if (-1 == new_key) {
			cout << "MAX USER EXCEEDED!!!\n";
			continue;
		}
		cout << "New Client's ID = " << new_key << endl;
		g_clients[new_key].s = new_socket;
		g_clients[new_key].x = 4;
		g_clients[new_key].y = 4;
		ZeroMemory(&g_clients[new_key].exover.wsaover, sizeof(WSAOVERLAPPED));
		CreateIoCompletionPort(reinterpret_cast<HANDLE>(new_socket),
			g_iocp, new_key, 0);
		g_clients[new_key].viewPlayerlist.clear();

		g_clients[new_key].viewNPClist.clear();

		g_clients[new_key].in_use = true;
		unsigned long flag = 0;
		int ret = WSARecv(new_socket, &g_clients[new_key].exover.wsabuf, 1,
			NULL, &flag, &g_clients[new_key].exover.wsaover, NULL);
		if (0 != ret) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("Recv in AcceptThread", err_no);
		}

		sc_packet_put_player p;
		p.id = new_key;
		p.size = sizeof(sc_packet_put_player);
		p.type = SC_PUT_PLAYER;
		p.x = g_clients[new_key].x;
		p.y = g_clients[new_key].y;
		// 나의 접속을 모든 플레이어에게 알림
		for (int i = 0; i < MAX_USER; ++i) {
			if (true == g_clients[i].in_use) {
				if (false == CanSeePlayer(i, new_key)) continue;
				SendPacket(i, &p);
				if (i == new_key) continue;
				g_clients[i].vlm.lock();
				g_clients[i].viewPlayerlist.insert(new_key);
				g_clients[i].vlm.unlock();
			}
		}
		// 나에게 접속해 있는 다른 플레이어 정보를 전송
		// 나에게 주위에 있는 NPC의 정보를 전송
		for (int i = 0; i < MAX_USER; ++i) {
			if (true == g_clients[i].in_use)
				if (i != new_key) {
					if (false == CanSeePlayer(i, new_key)) continue;
					p.id = i;
					p.x = g_clients[i].x;
					p.y = g_clients[i].y;
					g_clients[new_key].vlm.lock();
					g_clients[new_key].viewPlayerlist.insert(i);
					g_clients[new_key].vlm.unlock();
					SendPacket(new_key, &p);
				}
		}
		for (int i = 0; i < NUM_OF_NPC; i++) {
			if (false == CanSeeNPC(new_key, i)) continue;
			p.id = i;
			p.type = SC_PUT_OBJECT;
			p.x = g_npcs[i].x;
			p.y = g_npcs[i].y;
			g_clients[new_key].vlm.lock();
			g_clients[new_key].viewNPClist.insert(i);
			g_clients[new_key].vlm.unlock();
			WakeUpNPC(i);
			SendPacket(new_key, &p);
		}
	}
}

void HeartBeat(int i)
{
	bool move_ok = false;

	unordered_set <int> old_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSeeNPC(id, i)) {
				old_vl.insert(id);
				move_ok = true;
			}

	if (false == move_ok) return;

	switch (rand() % 4) {
	case 0: if (g_npcs[i].y > 0) g_npcs[i].y--; break;
	case 1: if (g_npcs[i].y < BOARD_HEIGHT - 1) g_npcs[i].y++; break;
	case 2: if (g_npcs[i].x > 0) g_npcs[i].x--; break;
	case 3: if (g_npcs[i].x < BOARD_WIDTH - 1) g_npcs[i].x++; break;
	default: break;
	}

	volatile int k = 0;
	for (int j = 0; j < 10000; ++j) k = k + j;

	unordered_set<int> new_vl;
	for (int id = 0; id < MAX_USER; ++id)
		if (true == g_clients[id].in_use)
			if (true == CanSeeNPC(id, i))
				new_vl.insert(id);

	sc_packet_pos p_packet;
	p_packet.id = i;
	p_packet.size = sizeof(p_packet);
	p_packet.type = SC_POS_OBJECT;
	p_packet.x = g_npcs[i].x;
	p_packet.y = g_npcs[i].y;

	// PutObject
	for (auto id : new_vl) {
		g_clients[id].vlm.lock();
		if (0 == g_clients[id].viewNPClist.count(i)) {
			g_clients[id].viewNPClist.insert(i);
			g_clients[id].vlm.unlock();
			SendPutObject(id, i);
		}
		else {
			g_clients[id].vlm.unlock();
			SendPacket(id, &p_packet);
		}
	}
	// RemoveObject
	for (auto id : old_vl) {
		if (0 == new_vl.count(id)) {
			g_clients[id].vlm.lock();
			if (0 != g_clients[id].viewNPClist.count(i)) {
				g_clients[id].viewNPClist.erase(i);
				g_clients[id].vlm.unlock();
				SendRemoveObject(id, i);
			}
			else {
				g_clients[id].vlm.unlock();
			}
		}
	}
}

void AIThread()
{
	while (true) {
		for (int i = 0; i < NUM_OF_NPC; ++i) {
			if (g_npcs[i].last_tick_time > GetTickCount() - 1000) 
				continue;
			HeartBeat(i);
			g_npcs[i].last_tick_time = GetTickCount();
		}
	}
}


int main()
{
	vector <thread> w_threads;
	Initialize();

	for (int i = 0; i < 4; ++i)
		w_threads.push_back(thread{ WorkerThread });

	thread a_thread{ AcceptThread };

	//thread t_thread{ timer_thread };

	thread ai_thread{ AIThread };
	for (auto &th : w_threads) th.join();
	a_thread.join();
	ai_thread.join();
	//t_thread.join();
	WSACleanup();
}

