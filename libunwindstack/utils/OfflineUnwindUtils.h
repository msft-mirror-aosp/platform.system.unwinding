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

#ifndef _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H
#define _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H

#include <string>

namespace unwindstack {

std::string GetOfflineFilesDirectory();

}  // namespace unwindstack

#endif  // _LIBUNWINDSTACK_UTILS_OFFLINE_UNWIND_UTILS_H