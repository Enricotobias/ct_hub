#pragma once

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

#ifndef CT_DEBUG_SERIAL_ENABLED
#define CT_DEBUG_SERIAL_ENABLED 1
#endif

inline void ctDebugLog(const char* level, const char* component,
                       const char* format, ...) {
#if CT_DEBUG_SERIAL_ENABLED
  char message[224]{};
  va_list arguments;
  va_start(arguments, format);
  const int length = std::vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  if (length < 0) return;
  Serial.printf("[%lu][%s][%s] %s\n", static_cast<unsigned long>(millis()),
                level == nullptr ? "INFO" : level,
                component == nullptr ? "CTHUB" : component, message);
#else
  (void)level;
  (void)component;
  (void)format;
#endif
}
