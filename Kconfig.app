# Copyright (c) 2017 Linaro Limited
#
# SPDX-License-Identifier: Apache-2.0
#

config APP_PWM_WHITE
	bool
	prompt "Enable dedicated PWM for white color"
	default y

if APP_PWM_WHITE

config APP_PWM_WHITE_DEV
	string
	prompt "PWM device name used for white"

config APP_PWM_WHITE_PIN
	int
	prompt "PWM pin number used for white"
	default 0

config APP_PWM_WHITE_PIN_CEILING
	int
	prompt "PWM pin level ceiling"
	default 255
	range 1 255

endif # APP_PWM_WHITE
