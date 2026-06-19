/*
 * Tiku Operating System
 * http://tiku-os.org
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_app_config.h - Application selection configuration
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

/**
 * @file tiku_app_config.h
 * @brief Application selection and configuration
 *
 * Included automatically when HAS_APPS is defined (via make APP=cli).
 * Each app has its own config header pulled in below.
 */

#ifndef TIKU_APP_CONFIG_H_
#define TIKU_APP_CONFIG_H_

/** Master application enable */
#define TIKU_APPS_ENABLE 0

/*---------------------------------------------------------------------------*/
/* CLI APPLICATION                                                           */
/*---------------------------------------------------------------------------*/

#if defined(TIKU_APP_CLI)
/* CLI has moved to kernel/shell — config included via tiku.h when
 * TIKU_SHELL_ENABLE is set (which APP=cli sets automatically). */
#endif

/*---------------------------------------------------------------------------*/
/* NET APPLICATION                                                           */
/*---------------------------------------------------------------------------*/

/* APP=net has no additional config — all tuning lives in
 * tikukits/net/tiku_kits_net.h (MTU, IP address, TTL, poll rate). */

#endif /* TIKU_APP_CONFIG_H_ */
