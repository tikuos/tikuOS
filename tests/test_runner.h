/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * test_runner.h - Test runner interface
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_RUNNER_H_
#define TEST_RUNNER_H_

#include "tiku.h"

/**
 * @brief Run all enabled tests
 *
 * Dispatches to individual test modules based on the TEST_* flags
 * configured in tiku.h. Handles interrupt enable for timer tests.
 */
void test_run_all(void);

#endif /* TEST_RUNNER_H_ */
