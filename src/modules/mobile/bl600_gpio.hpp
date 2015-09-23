#pragma once

#include <stm32.h>

#include <board_config.h>

#include "debug.hpp"

namespace bl600
{

void
reset()
{
	dbg("bl600 resetting...\n");
	stm32_gpiowrite(GPIO_BL600_RESET, 0);
	sleep(3);
	stm32_gpiowrite(GPIO_BL600_RESET, 1);
	sleep(3);
	dbg("bl600 reset done...\n");
}

void
mode_AT()
{
	stm32_gpiowrite(GPIO_BL600_SIO_07, 0);
	stm32_gpiowrite(GPIO_BL600_SIO_28, 1);
	reset();
}

void
mode_default()
{
	stm32_configgpio(GPIO_BL600_SIO_07);
	stm32_configgpio(GPIO_BL600_SIO_28);
	reset();
}

} // end of namespace bl600