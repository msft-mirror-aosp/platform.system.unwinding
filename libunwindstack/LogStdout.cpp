/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#include <android-base/stringprintf.h>

#include <unwindstack/Log.h>

namespace unwindstack {

// This code will always print to stdout.
void log_to_stdout(bool) {}

// Send the data to the log.
void log(uint8_t indent, const char* format, ...) {
  std::string real_format;
  if (indent > 0) {
    real_format = android::base::StringPrintf("%*s%s", 2 * indent, " ", format);
  } else {
    real_format = format;
  }
  real_format += '\n';

  va_list args;
  va_start(args, format);
  vprintf(real_format.c_str(), args);
  va_end(args);
}

// Do nothing for async safe.
void log_async_safe(const char*, ...) {}

}  // namespace unwindstack
