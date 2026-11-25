// Sound alerts (alerts.cpp). networkTask calls this once per poll.
#pragma once
// liveOk = a good live poll just landed; wentOffline = the board just lost the server.
void pollAlerts(bool liveOk, bool wentOffline);
