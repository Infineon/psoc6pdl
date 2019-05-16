################################################################################
# \file module.mk
# \version 2.40
#
# \brief
# This is the PSoC 6 linker script SW component for the CM4 core
#
################################################################################
# \copyright
# Copyright 2018-2019 Cypress Semiconductor Corporation
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

ifeq ($(WHICHFILE),true)
$(info Processing $(lastword $(MAKEFILE_LIST)) file from directory $(PWD))
$(info Path: $(MAKEFILE_LIST))
endif

#
# Needed by describe goal processing
#
ifeq ($(MAKECMDGOALS),describe)
ifndef PLATFORMS_VERSION
PLATFORMS_VERSION=1.0
endif
include $(CYSDK)/libraries/platforms-$(PLATFORMS_VERSION)/common/swcomps.mk
endif


#
# The internal tag name of the software component
#
PSOC6_CM4_LINKER_SCRIPT_NAME = PSOC6_CM4_LINKER_SCRIPT

#
# If defined, the list of legal PLATFORM values for this component.
# If not defined, this component is valid for all values of PLATFORM
#
CY_SUPPORTED_PLATFORM_LIST=PSOC6_DUAL_CORE PSoC6_cm4_dual PSoC6_cm0p PSOC6_SINGLE_CORE PSoC6_cm4_single

#
# If defined, the list of legal TOOLCHAIN values for this component. If not
# defined, this component is valid for all values of TOOLCHAIN
#
#CY_SUPPORTED_TOOLCHAIN_LIST=

#
# Used by the IDE to group and categorize components.
#
CY_COMP_CATEGORY=LinkerScript

#
# The displayed human readable name of the component
#
CY_COMP_NAME_DISPLAY=PSoC6 CM4 linker script

#
# The name in the form of an identifier ([a-z_][a-z0-9_]*).
# Used to generate directories in the IDE.
# 
CY_COMP_NAME_ID=psoc6Cm4Linker

#
# The human readable description of the component
#
CY_COMP_DESCR=This is the PSoC 6 CM4 linker script

#
# CY_VISIBLE will decide whether to show the component in middleware configurator or not.
# If CY_VISIBLE set to Y then it will be shown up in middleware. If CY_VISIBLE set to N then
# component will not be shown up in middleware.
#
CY_VISIBLE=N

#
# The type of component
#   link - means link the source code from the IDE project to the SDK
#   copy - means copy the source code into the IDE project preserving the SDK directory structure
#   copyto - means copy the source code into the IDE project
#
CY_COMP_TYPE=copyto

#
# Defines if the component can change based on which artifact is being used
#
CY_COMP_PER_ARTIFACT=false

#
# The list of components this component is dependent on. Path is relative the
# the SDK's libraries folder.
#
CY_COMP_DEPS=

#
# Used by the build recipe for an ELF file to add this component
# to the list of components that must be included
#
$(CYARTIFACT)_OBJ_COMP_TAG_LIST += $(PSOC6_CM4_LINKER_SCRIPT_NAME)

#
# Linker script name
#
ifneq (,$(findstring $(DEVICE),$(filter $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_DIE_PSOC6A512K))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xx5_cm4_dual
else ifneq (,$(findstring $(DEVICE),$(filter-out $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_512))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xx6_cm4
else ifneq (,$(findstring $(DEVICE),$(filter $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_512))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xx6_cm4_dual
else ifneq (,$(findstring $(DEVICE),$(filter-out $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_1024))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xx7_cm4
else ifneq (,$(findstring $(DEVICE),$(filter $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_1024))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xx7_cm4_dual
else ifneq (,$(findstring $(DEVICE),$(filter-out $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_2048))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xxa_cm4
else ifneq (,$(findstring $(DEVICE),$(filter $(CY_DEVICES_WITH_M0P), $(CY_DEVICES_WITH_FLASH_KB_2048))))
CY_CM4_LINKER_SCRIPT_NAME = cy8c6xxa_cm4_dual
else
# Unsupported part number
endif

ifeq ($(TOOLCHAIN),GCC)
CY_CM4_LINKER_SCRIPT = TOOLCHAIN_GCC_ARM/$(CY_CM4_LINKER_SCRIPT_NAME).ld
else ifeq ($(TOOLCHAIN),IAR)
CY_CM4_LINKER_SCRIPT = TOOLCHAIN_IAR/$(CY_CM4_LINKER_SCRIPT_NAME).icf
else ifeq ($(TOOLCHAIN),ARMCC)
CY_CM4_LINKER_SCRIPT = TOOLCHAIN_ARM/$(CY_CM4_LINKER_SCRIPT_NAME).scat
endif

#
# Defines the series of needed make variables that include this component in the
# build process.  Also defines the describe target if we are describing a component
#

$(eval $(call \
	CY_DECLARE_SWCOMP_OBJECT,$(PSOC6_CM4_LINKER_SCRIPT_NAME),\
	$(lastword $(MAKEFILE_LIST)),\
	$(CY_CM4_LINKER_SCRIPT)))
