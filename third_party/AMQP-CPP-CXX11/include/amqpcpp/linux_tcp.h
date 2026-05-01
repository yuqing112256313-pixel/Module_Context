#pragma once

#if defined(_WIN32)
#error "amqpcpp/linux_tcp.h is only available on POSIX systems. Disable AMQP-CPP_LINUX_TCP on Windows builds."
#endif

#include "linux_tcp/tcpparent.h"
#include "linux_tcp/tcphandler.h"
#include "linux_tcp/tcpconnection.h"
#include "linux_tcp/tcpchannel.h"
