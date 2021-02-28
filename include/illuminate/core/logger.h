#ifndef ILLUMINATE_CORE_LOGGER_H
#define ILLUMINATE_CORE_LOGGER_H
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h" // for custom class logging
#define logtrace spdlog::trace
#define logdebug spdlog::debug
#define loginfo  spdlog::info
#define logwarn  spdlog::warn
#define logerror spdlog::error
#define logfatal spdlog::critical
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif