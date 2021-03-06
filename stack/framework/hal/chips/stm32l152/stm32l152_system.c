/* * OSS-7 - An opensource implementation of the DASH7 Alliance Protocol for ultra
 * lowpower wireless sensor communication
 *
 * Copyright 2015 University of Antwerp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*! \file stm32l152_system.c
 *
 *
 */


#include "hwsystem.h"
#include <assert.h>
#include "stm32l1xx_hal.h"
#include "stm32l1xx_hal_pwr.h"

void hw_enter_lowpower_mode(uint8_t mode)
{
  switch (mode)
  {
    case 0:
    case 1:
      HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
      break;

  }
}

uint64_t hw_get_unique_id()
{
  return (uint64_t) HAL_GetDEVID();
}

#pragma GCC push_options
#pragma GCC optimize ("O3")
void hw_busy_wait(int16_t microseconds)
{
  DWT->CTRL |= 1 ;
  volatile uint32_t cycles = (SystemCoreClock/1000000L)*microseconds;
  volatile uint32_t start = DWT->CYCCNT;
  do {
  } while(DWT->CYCCNT - start < cycles);
}
#pragma GCC pop_options

void hw_reset()
{
  HAL_NVIC_SystemReset();
}

void HardFault_Handler()
{
  assert(false);
}
