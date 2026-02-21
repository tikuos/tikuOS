/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_cpu.h - Platform-agnostic CPU abstraction interface
 *
 * Provides atomic section entry/exit for interrupt-safe operations.
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

 #ifndef TIKU_CPU_H_
 #define TIKU_CPU_H_

 void tiku_atomic_enter(); // Enter atomic section
 void tiku_atomic_exit(); // Exit atomic section

 void tiku_cpu_boot_init(void);
 void tiku_cpu_freq_init(unsigned int cpu_freq);

 #endif /* TIKU_CPU_H_ */
