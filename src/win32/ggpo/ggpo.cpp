#include "wsnes9x.h"
#include "snapshot.h"
#include "ggponet.h"
#include "ggpoclient.h"
#include "ggpo.h"
#include "ggpo_perfmon.h"

#define QUARKS_VERSION 2

GGPOSession *ggpo = NULL;
bool bSkipPerfmonUpdates = false;

bool ProcessFrame();
bool ProcessFrameDelay(int delay);
bool LoadROM(const TCHAR *filename, const TCHAR *filename2 = NULL);

void QuarkInitPerfMon();
void QuarkPerfMonUpdate(GGPONetworkStats *stats);

int kNetGame = 0;
int kNetSpectator = 0;

// average helper
template <int _size>
struct Averager {
	int current = 0;
	int data[_size] = {};
	float average = 0.f;
	float deviation = 0.f;
	void Update(int value) {
		data[current] = value;
		current = (current + 1) % _size;
		// average
		average = 0.f;
		for (int i = 0; i < _size; i++) {
			average += data[i];
		}
		average = average / _size;
	}
};

static Averager<60> LocalFramesBehind;
static Averager<60> RemoteFramesBehind;
static Averager<90> FramesBehind;
static Averager<10> FrameTimes;

static bool bDirect = false;
static int iPlayer = 0;
static int iDelay = 0;

bool __cdecl ggpo_on_client_event_callback(GGPOClientEvent *info)
{
  switch (info->code)
  {
  case GGPOCLIENT_EVENTCODE_CONNECTING:
    //VidOverlaySetSystemMessage(_T("Connecting..."));
    break;

  case GGPOCLIENT_EVENTCODE_CONNECTED:
    //VidOverlaySetSystemMessage(_T("Connected"));
    break;

  case GGPOCLIENT_EVENTCODE_RETREIVING_MATCHINFO:
    //VidOverlaySetSystemMessage(_T("Retrieving Match Info..."));
    break;

  case GGPOCLIENT_EVENTCODE_DISCONNECTED:
    //VidOverlaySetSystemMessage(_T("Disconnected!"));
    break;

  case GGPOCLIENT_EVENTCODE_MATCHINFO: {
		//VidOverlaySetSystemMessage(_T(""));
		//kNetVersion = strlen(info->u.matchinfo.blurb) > 0 ? atoi(info->u.matchinfo.blurb) : QUARKS_VERSION;
		//TCHAR szUser1[128];
		//TCHAR szUser2[128];
    //VidOverlaySetGameInfo(ANSIToTCHAR(info->u.matchinfo.p1, szUser1, 128), ANSIToTCHAR(info->u.matchinfo.p2, szUser2, 128), kNetSpectator, iRanked, iPlayer);
    break;
	}

  case GGPOCLIENT_EVENTCODE_SPECTATOR_COUNT_CHANGED:
    //VidOverlaySetGameSpectators(info->u.spectator_count_changed.count);
    break;

  case GGPOCLIENT_EVENTCODE_CHAT:
    if (strlen(info->u.chat.text) > 0) {
			//TCHAR szUser[128];
      //TCHAR szText[1024];
      //ANSIToTCHAR(info->u.chat.username, szUser, 128);
      //ANSIToTCHAR(info->u.chat.text, szText, 1024);
      //VidOverlayAddChatLine(szUser, szText);
    }
    break;

  default:
    break;
  }
  return true;
}

bool __cdecl ggpo_on_client_game_callback(GGPOClientEvent *info)
{
  // DEPRECATED
  return true;
}

bool __cdecl ggpo_on_event_callback(GGPOEvent *info)
{
  if (ggpo_is_client_eventcode(info->code)) {
    return ggpo_on_client_event_callback((GGPOClientEvent *)info);
  }
  if (ggpo_is_client_gameevent(info->code)) {
    return ggpo_on_client_game_callback((GGPOClientEvent *)info);
  }
  switch (info->code) {
  case GGPO_EVENTCODE_CONNECTED_TO_PEER:
    //VidOverlaySetSystemMessage(_T("Connected to Peer"));
    break;

  case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
    //_stprintf(status, _T("Synchronizing with Peer (%d/%d)..."), info->u.synchronizing.count, info->u.synchronizing.total);
    //VidOverlaySetSystemMessage(_T("Synchronizing with Peer..."));
    break;

  case GGPO_EVENTCODE_RUNNING:
    //VidOverlaySetSystemMessage(_T(""));
    break;

  case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
    //VidOverlaySetSystemMessage(_T("Disconnected from Peer"));
    break;

  case GGPO_EVENTCODE_TIMESYNC:
		// GGPO timesync is bad
    break;

  default:
    break;
  }

  return true;
}

bool __cdecl ggpo_begin_game_callback(char *name)
{
	TCHAR filename[MAX_PATH];
	swprintf(filename, _T("%hs.zip"), name);
	LoadROM(filename);
	//S9xUnfreezeGame(Settings.InitialSnapshotFilename);

	return 0;
}

bool __cdecl ggpo_advance_frame_callback(int flags)
{
  bSkipPerfmonUpdates = true;
	ProcessFrame();
	bSkipPerfmonUpdates = false;
  return true;
}


void QuarkProcessEndOfFrame()
{
}

const int ggpo_state_header_size = 4 * sizeof(int);
static const int ACB_BUFFER_SIZE = 16 * 1024 * 1024;
static char gAcbBuffer[ACB_BUFFER_SIZE];

static int QuarkLogAcb(struct BurnArea* pba)
{
  return 0;
}

bool __cdecl ggpo_save_game_state_callback(unsigned char **buffer, int *len, int *checksum, int frame)
{
	S9xFreezeGameMem((uint8 *)gAcbBuffer, ACB_BUFFER_SIZE);
	int payloadsize = S9xFreezeSize();

  *checksum = 0;
  *len = payloadsize + ggpo_state_header_size;
  *buffer = (unsigned char *)malloc(*len);

  int *data = (int *)*buffer;
  data[0] = 'GGPO';
  data[1] = ggpo_state_header_size;
  data[2] = 0;
  data[3] = 0;
  memcpy((*buffer) + ggpo_state_header_size, gAcbBuffer, payloadsize);
  return false;
}

bool __cdecl ggpo_load_game_state_callback(unsigned char *buffer, int len)
{
  int *data = (int *)buffer;
  if (data[0] == 'GGPO') {
    int headersize = data[1];
    buffer+= headersize;
		S9xUnfreezeGameMem(buffer, len - headersize);
  }

  return true;
}

bool __cdecl ggpo_log_game_state_callback(char *filename, unsigned char *buffer, int len)
{
  /*
   * Note: this is destructive since it relies on loading game
   * state before scanning!  Luckily, we only call the logging
   * routine for fatal errors (we should still fix this, though).
   */
  ggpo_load_game_state_callback(buffer, len);

  //gAcbLogFp = fopen(filename, "w");

  //gAcbChecksum = 0;
  //BurnAcb = QuarkLogAcb;
  //BurnAreaScan(ACB_FULLSCANL | ACB_READ, NULL);
  //fprintf(gAcbLogFp, "\n");
  //fprintf(gAcbLogFp, "Checksum:       %d\n", gAcbChecksum);
  //fprintf(gAcbLogFp, "Buffer Pointer: %p\n", buffer);
  //fprintf(gAcbLogFp, "Buffer Len:     %d\n", len);

  //fclose(gAcbLogFp);

  return true;
}

void __cdecl ggpo_free_buffer_callback(void *buffer)
{
  free(buffer);
}

// ggpo_set_frame_delay from lib
typedef INT32(_cdecl *f_ggpo_set_frame_delay)(GGPOSession *, int frames);
static f_ggpo_set_frame_delay ggpo_set_frame_delay;

static bool ggpo_init()
{
  // load missing ggpo_set_frame_delay from newer ggponet.dll, not available on ggponet.lib
  HINSTANCE hLib = LoadLibrary(_T("ggponet.dll"));
  if (!hLib) {
    return false;
	}
  ggpo_set_frame_delay = (f_ggpo_set_frame_delay)GetProcAddress(hLib, "ggpo_set_frame_delay");
  if (!ggpo_set_frame_delay) {
    return false;
	}

  FreeLibrary(hLib);
  return true;
}

void QuarkInit(const TCHAR *tconnect)
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	//QuarkTogglePerfMon();

  ggpo_init();

	char connect[MAX_PATH];
  char game[128], quarkid[128], server[128];
  int port = 0;
  int delay = 0;
  int ranked = 0;
  int live = 0;
  int frames = 0;
  int player = 0;
  int localPort, remotePort;

  kNetGame = 1;
	kNetSpectator = 0;
  iPlayer = 0;
	iDelay = 0;

  GGPOSessionCallbacks cb = { 0 };

	strcpy(connect, _tToChar(tconnect));

  cb.begin_game = ggpo_begin_game_callback;
  cb.load_game_state = ggpo_load_game_state_callback;
  cb.save_game_state = ggpo_save_game_state_callback;
  cb.log_game_state = ggpo_log_game_state_callback;
  cb.free_buffer = ggpo_free_buffer_callback;
  cb.advance_frame = ggpo_advance_frame_callback;
  cb.on_event = ggpo_on_event_callback;

  if (strncmp(connect, "quark:served", strlen("quark:served")) == 0) {
    sscanf(connect, "quark:served,%[^,],%[^,],%d,%d", game, quarkid, &port, &delay);
    iPlayer = atoi(&quarkid[strlen(quarkid)-1]);
		iDelay = delay;
    ggpo = ggpo_client_connect(&cb, game, quarkid, port);
    ggpo_set_frame_delay(ggpo, delay);
		//VidOverlaySetSystemMessage(_T("Connecting..."));
  }
  else if (strncmp(connect, "quark:direct", strlen("quark:direct")) == 0) {
    sscanf(connect, "quark:direct,%[^,],%d,%[^,],%d,%d,%d", game, &localPort, server, &remotePort, &player, &delay);
    bDirect = true;
    iPlayer = player;
		iDelay = delay;
    ggpo = ggpo_start_session(&cb, game, localPort, server, remotePort, player);
    ggpo_set_frame_delay(ggpo, delay);
		//VidOverlaySetSystemMessage(_T("Connecting..."));
  }
	/*
  else if (strncmp(connect, "quark:synctest", strlen("quark:synctest")) == 0) {
    sscanf(connect, "quark:synctest,%[^,],%d", game, &frames);
    ggpo = ggpo_start_synctest(&cb, game, frames);
  }
	*/
  else if (strncmp(connect, "quark:stream", strlen("quark:stream")) == 0) {
    sscanf(connect, "quark:stream,%[^,],%[^,],%d", game, quarkid, &remotePort);
		kNetSpectator = 1;
    ggpo = ggpo_start_streaming(&cb, game, quarkid, remotePort);
		//VidOverlaySetSystemMessage(_T("Connecting..."));
  }
  else if (strncmp(connect, "quark:replay", strlen("quark:replay")) == 0) {
    ggpo = ggpo_start_replay(&cb, connect + strlen("quark:replay,"));
  }
}

void QuarkEnd()
{
	ggpo_close_session(ggpo);
  kNetGame = 0;
}

void QuarkTogglePerfMon()
{
  static bool initialized = false;
  if (!initialized) {
    ggpoutil_perfmon_init(GUI.hWnd);
  }
  ggpoutil_perfmon_toggle();
}

void QuarkRunIdle(int ms)
{
	// update GGPO
	ggpo_idle(ggpo, ms);
}

bool QuarkGetInput(void *values, int size, int players)
{
  return ggpo_synchronize_input(ggpo, values, size, players);
}

bool QuarkIncrementFrame()
{
  ggpo_advance_frame(ggpo);

	if (!bSkipPerfmonUpdates) {
		GGPONetworkStats stats;
		ggpo_get_stats(ggpo, &stats);
		ggpoutil_perfmon_update(ggpo, stats);

		LocalFramesBehind.Update(stats.timesync.local_frames_behind);
		RemoteFramesBehind.Update(stats.timesync.remote_frames_behind);
		FramesBehind.Update(stats.timesync.local_frames_behind - stats.timesync.remote_frames_behind);
		int frames_behind = (int)(FramesBehind.average * 1000 / 2);
		// balance rift if ahead or behind the connection (one to go slightly faster, the other slightly slower! win win)
		if (abs(frames_behind) > 1000) {
			static int time = 0;
			int t = timeGetTime();
			// balance rift
			if ((t - time) > 1000.f) {
				if (frames_behind < -5000) frames_behind = -5000;
				if (frames_behind > 5000) frames_behind = 5000;
				ProcessFrameDelay(-frames_behind);
				time = t;
			}
		}
  }

  return true;
}

void QuarkSendChatText(char *text)
{
  //QuarkSendChatCmd(text, 'T');
}

void QuarkSendChatCmd(char *text, char cmd)
{
  char buffer[1024]; // command chat
  buffer[0] = cmd;
  strcpy(&buffer[1], text);
  ggpo_client_chat(ggpo, buffer);
}

void QuarkUpdateStats(double fps)
{
  GGPONetworkStats stats;
  ggpo_get_stats(ggpo, &stats);
  //VidOverlaySetStats(fps, stats.network.ping, iDelay);
}
