#ifndef __LOGGER_H__
#define __LOGGER_H__
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h" // for custom class logging
#define logtrace spdlog::trace
#define logdebug spdlog::debug
#define loginfo  spdlog::info
#define logwarn  spdlog::warn
#define logerror spdlog::error
#define logfatal spdlog::critical
#endif
