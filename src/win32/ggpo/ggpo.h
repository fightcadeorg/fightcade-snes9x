#ifndef _GGPO_H_
#define _GGPO_H_

extern int kNetGame;
extern int kNetSpectator;

void QuarkInit(const TCHAR *connect);
void QuarkEnd();
void QuarkTogglePerfMon();
void QuarkRunIdle(int ms);
bool QuarkGetInput(void *values, int size, int players);
bool QuarkIncrementFrame();
void QuarkSendChatText(char *text);
void QuarkSendChatCmd(char *text, char cmd);
void QuarkUpdateStats(double fps);

#endif