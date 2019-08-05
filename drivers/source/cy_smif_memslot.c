/***************************************************************************//**
* \file cy_smif_memslot.c
* \version 1.30
*
* \brief
*  This file provides the source code for the memory-level APIs of the SMIF driver.
*
* Note:
*
********************************************************************************
* \copyright
* Copyright 2016-2019 Cypress Semiconductor Corporation
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#include "cy_smif_memslot.h"

#ifdef CY_IP_MXSMIF

#if defined(__cplusplus)
extern "C" {
#endif

/** \cond INTERNAL */
/***************************************
*     Internal Constants
***************************************/

#define READ_ENHANCED_MODE_DISABLED (0xFFU)
#define BITS_IN_BYTE               (8U)
#define BYTES_IN_DWORD             (4U)
#define BITS_IN_BYTE_ABOVE_4GB     (3U)     /** Density of memory above 4GBit stored as poser of 2 */
#define PARAM_HEADERS_NUM          (CY_SMIF_SFDP_BFPT_BYTE_06)
#define FIRST_HEADER_OFFSET        (0x08U)  /** The offset of the 1-st Parameter Header */
#define PARAM_ID_MSB_REL_OFFSET    (0x07U)  /** The relative offset of Parameter ID MSB 
                                                          * in the SFDP Header table 
                                                          */
#define PARAM_MINOR_REV_REL_OFFSET (0x01U)  /** The relative offset of Parameter Minor Revision 
                                                          * in the SFDP Header table 
                                                          */
#define PARAM_MAJOR_REV_REL_OFFSET (0x02U)  /** The relative offset of Parameter Major Revision
                                                          * in the SFDP Header table
                                                          */
#define PARAM_ID_MSB_OFFSET        (0x08U)  /** The offset of Parameter ID MSB */
#define PARAM_ID_LSB_MASK          (0xFFUL) /** The mask of Parameter ID LSB */
#define PARAM_TABLE_PRT_OFFSET     (0x04UL) /** The relative offset of Parameter Table Pointer Byte 1 */
#define PARAM_TABLE_LENGTH_OFFSET  (0X03U)  /** The offset of Parameter Table Length in the Header Table */
#define PARAM_HEADER_NUM           (6U)     /** The supported number of the parameter headers */
#define HEADER_LENGTH              (0x8U)   /**< The length of the SFDP header */
#define HEADERS_LENGTH             (HEADER_LENGTH + \
                                                 (CY_SMIF_SFDP_PARAM_HEADER_LENGTH * PARAM_HEADER_NUM))
#define TYPE_STEP                  (2UL)     /** The Erase Type step in the Basic Flash Parameter Table */
#define INSTRUCTION_NOT_SUPPORTED  (0XFFU)  /** The code for the not supported instruction */
#define BASIC_SPI_ID_LSB           (0X00UL) /** The JEDEC SFDP Basic SPI Flash Parameter ID LSB */
#define BASIC_SPI_ID_MSB           (0XFFUL) /** The JEDEC SFDP Basic SPI Flash Parameter ID MSB */
#define ERASE_T_COUNT_Pos          (0UL)    /**< Erase Type X Erase, Typical time: count (Bits 4:0) */
#define ERASE_T_COUNT_Msk          (0x1FUL) /**< Erase Type X Erase, Typical time: count (Bitfield-Mask) */
#define ERASE_T_UNITS_Pos          (5UL)    /**< Erase Type X Erase, Typical time: units (Bits 6:5) */
#define ERASE_T_UNITS_Msk          (0x60UL) /**< Erase Type X Erase, Typical time: units (Bitfield-Mask) */
#define ERASE_T_COUNT_OFFSET       (0x04U)  /** The offset of the Erase count 10th DWORD */
#define ERASE_T_LENGTH             (0x07U)  /** The Erase Type Typical time length */
#define COMMAND_IS_NOT_FOUND       (0x0U) 
#define PARAMETER_IS_NOT_FOUND     (0x0U)

#define MEM_ADDR_VALID(addr, size)  (0U == ((addr)%(size)))  /* This address must be a multiple of 
                                                                      * the SMIF XIP memory size 
                                                                      */
#define MEM_MAPPED_SIZE_VALID(size) (((size) >= 0x10000U) && (0U == ((size)&((size)-1U))) )
#define MEM_ADDR_SIZE_VALID(addrSize)   ((0U < (addrSize)) && ((addrSize) <= 4U))

/** \endcond*/

/***************************************
*     Internal Function Prototypes
***************************************/
static void XipRegInit(SMIF_DEVICE_Type volatile *dev,
                            cy_stc_smif_mem_config_t const * memCfg);
static cy_en_smif_status_t SfdpReadBuffer(SMIF_Type *base, 
                                         cy_stc_smif_mem_cmd_t const *cmdSfdp,
                                         uint8_t const sfdpAddress[],
                                         cy_en_smif_slave_select_t  slaveSelect,
                                         uint32_t size,
                                         uint8_t sfdpBuffer[],
                                         cy_stc_smif_context_t *context);
static uint32_t SfdpFindParameterHeader(uint32_t id, uint8_t const sfdpBuffer[]);
static cy_en_smif_status_t SfdpFindParameterTableAddress(uint32_t id,
                                                     uint8_t const sfdpBuffer[],
                                                     uint8_t address[],
                                                     uint32_t *tableLength);
static uint32_t SfdpGetNumOfAddrBytes(uint8_t const sfdpBuffer[]);
static uint32_t SfdpGetMemoryDensity(uint8_t const sfdpBuffer[]);
static void SfdpGetReadCmd_1_4_4(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead);
static void SfdpGetReadCmd_1_1_4(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead);
static void SfdpGetReadCmd_1_2_2(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead);
static void SfdpGetReadCmd_1_1_2(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead);
static void SfdpGetReadCmd_1_1_1(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead);
static void SfdpGetReadCmdParams(uint8_t const sfdpBuffer[], 
                                      cy_en_smif_data_select_t dataSelect, 
                                      cy_stc_smif_mem_cmd_t* cmdRead);
static uint32_t SfdpGetPageSize(uint8_t const sfdpBuffer[]);
static uint32_t SfdpGetEraseTime(uint32_t const eraseOffset, uint8_t const sfdpBuffer[]);
static uint32_t SfdpGetChipEraseTime(uint8_t const sfdpBuffer[]);
static uint32_t SfdpGetPageProgramTime(uint8_t const sfdpBuffer[]);
static void SfdpSetWriteEnableCommand(cy_stc_smif_mem_cmd_t* cmdWriteEnable);
static void SfdpSetWriteDisableCommand(cy_stc_smif_mem_cmd_t* cmdWriteDisable);
static void SfdpSetProgramCommand(cy_stc_smif_mem_cmd_t* cmdProgram);
static void SfdpGetQuadEnableParameters(cy_stc_smif_mem_device_cfg_t *device, 
                                    uint8_t const sfdpBuffer[]);
static void SfdpSetChipEraseCommand(cy_stc_smif_mem_cmd_t* cmdChipErase);
static uint32_t SfdpGetSectorEraseCommand(cy_stc_smif_mem_cmd_t* cmdSectorErase, 
                                   uint8_t const sfdpBuffer[]);
static void SfdpSetWipStatusRegisterCommand(cy_stc_smif_mem_cmd_t* readStsRegWipCmd);


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_Init
****************************************************************************//**
*
* This function initializes the slots of the memory device in the SMIF
* configuration. 
* If the user applies the external memory as memory-mapped 
* to PSoC (XIP mode), after such initialization, the memory slave devices are 
* automatically mapped into the PSoC memory map. The function needs the SMIF
* to be running in the memory mode to have the memory mapped into the PSoC
* address space. This function is typically called in the System initialization
* phase to initialize all the memory-mapped SMIF devices.
* This function only configures the memory device portion of the SMIF
* initialization and therefore assumes that the SMIF blocks initialization is
* achieved using Cy_SMIF_Init(). The cy_stc_smif_context_t context structure
* returned from Cy_SMIF_Init() is passed as a parameter to this function.
* This function calls the \ref Cy_SMIF_Memslot_SfdpDetect() function for each
* element of the \ref cy_stc_smif_mem_config_t memConfig array and fills 
* the memory parameters if the autoDetectSfdp field is enabled 
* in \ref cy_stc_smif_mem_config_t.
* The filled memConfig is a part of the 
* \ref cy_stc_smif_block_config_t * blockConfig structure. The function expects 
* that all the requirements of \ref Cy_SMIF_Memslot_SfdpDetect() is provided.
*
* \param base
* The address of the slave-slot device register to initialize.
*
* \param blockConfig
* The configuration structure array that configures the SMIF memory device to be
* mapped into the PSoC memory map. \ref cy_stc_smif_mem_config_t
*
* \param context
* The SMIF internal context structure of the block.
*
* \return The memory slot initialization status.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_BAD_PARAM
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_Init(SMIF_Type *base,
                            cy_stc_smif_block_config_t * const blockConfig,
                            cy_stc_smif_context_t *context)
{
    SMIF_DEVICE_Type volatile * device;
    cy_stc_smif_mem_config_t const * memCfg;
    uint32_t result = (uint32_t)CY_SMIF_BAD_PARAM;
    uint32_t sfdpRes =(uint32_t)CY_SMIF_SUCCESS;
    uint32_t idx;

    if ((NULL != base) && (NULL != blockConfig) && (NULL != blockConfig->memConfig) 
        && (NULL != context) && (0U != blockConfig->memCount))
    {
        uint32_t size = blockConfig->memCount;
        cy_stc_smif_mem_config_t** extMemCfg = blockConfig->memConfig;

        result = (uint32_t)CY_SMIF_SUCCESS;
        for(idx = 0UL; idx < size; idx++)
        {
            memCfg = extMemCfg[idx];
            if (NULL != memCfg)
            {
                /* Check smif memory slot configuration*/
                CY_ASSERT_L3(CY_SMIF_SLAVE_SEL_VALID(memCfg->slaveSelect));
                CY_ASSERT_L3(CY_SMIF_DATA_SEL_VALID(memCfg->dataSelect));
                CY_ASSERT_L1(NULL != memCfg->deviceCfg);
                CY_ASSERT_L2(MEM_ADDR_SIZE_VALID(memCfg->deviceCfg->numOfAddrBytes));
                
                device = Cy_SMIF_GetDeviceBySlot(base, memCfg->slaveSelect);
                if (NULL != device)
                {
                    /* The slave-slot initialization of the device control register.
                     * Cy_SMIF_Memslot_SfdpDetect() must work */
                    SMIF_DEVICE_CTL(device)  = _CLR_SET_FLD32U(SMIF_DEVICE_CTL(device),
                                                               SMIF_DEVICE_CTL_DATA_SEL,
                                                              (uint32_t)memCfg->dataSelect);
                    uint32_t sfdpRet = (uint32_t)CY_SMIF_SUCCESS;
                    if (0U != (memCfg->flags & CY_SMIF_FLAG_DETECT_SFDP))
                    {
                        sfdpRet = (uint32_t)Cy_SMIF_Memslot_SfdpDetect(base,
                                                memCfg->deviceCfg,
                                                memCfg->slaveSelect,
                                                memCfg->dataSelect,
                                                context);
                        if((uint32_t)CY_SMIF_SUCCESS != sfdpRet)
                        {
                            sfdpRes |=  ((uint32_t)CY_SMIF_SFDP_FAIL << idx);
                        }
                    }
                    if (((uint32_t)CY_SMIF_SUCCESS == sfdpRet) &&
                            (0U != (memCfg->flags & CY_SMIF_FLAG_MEMORY_MAPPED)))
                    {
                        /* Check valid parameters for XIP */
                        CY_ASSERT_L3(MEM_ADDR_VALID( memCfg->baseAddress, memCfg->memMappedSize));
                        CY_ASSERT_L3(MEM_MAPPED_SIZE_VALID( memCfg->memMappedSize));
                        
                        XipRegInit(device, memCfg);

                        /* The device control register initialization */
                        SMIF_DEVICE_CTL(device) = (memCfg->flags & CY_SMIF_FLAG_WR_EN) |
                                      (memCfg->flags & CY_SMIF_FLAG_CRYPTO_EN) |
                                      _VAL2FLD(SMIF_DEVICE_CTL_DATA_SEL,  (uint32_t)memCfg->dataSelect) |
                                      SMIF_DEVICE_CTL_ENABLED_Msk;
                    }
                }
                else
                {
                    result = (uint32_t)CY_SMIF_BAD_PARAM;
                    break;
                }
            }
        }
    }
    if((uint32_t)CY_SMIF_SUCCESS != sfdpRes)
    {
        result = CY_SMIF_ID | CY_PDL_STATUS_ERROR | sfdpRes;
    }
    return (cy_en_smif_status_t) result;
}


/*******************************************************************************
* Function Name: XipRegInit
****************************************************************************//**
*
* \internal
* This function initializes the memory device registers used for the XIP mode of
* the specified device.
*
* \param dev
* The SMIF memory device registers structure. \ref SMIF_DEVICE_Type
*
* \param memCfg
* The memory configuration structure that configures the SMIF memory device to
*  map into the PSoC memory map. \ref cy_stc_smif_mem_config_t
*
*******************************************************************************/
static void XipRegInit(SMIF_DEVICE_Type volatile *dev, cy_stc_smif_mem_config_t const * memCfg)
{
    cy_stc_smif_mem_device_cfg_t const * devCfg = memCfg->deviceCfg;
    cy_stc_smif_mem_cmd_t const * read = devCfg->readCmd;
    cy_stc_smif_mem_cmd_t const * prog = devCfg->programCmd;

    SMIF_DEVICE_ADDR(dev) = (SMIF_DEVICE_ADDR_ADDR_Msk & memCfg->baseAddress);

    /* Convert the size in the mask*/
    SMIF_DEVICE_MASK(dev)= (SMIF_DEVICE_MASK_MASK_Msk & (~(memCfg->memMappedSize) + 1UL));

    SMIF_DEVICE_ADDR_CTL(dev) = _VAL2FLD(SMIF_DEVICE_ADDR_CTL_SIZE2, (devCfg->numOfAddrBytes - 1UL)) |
                                ((0UL != memCfg->dualQuadSlots)? SMIF_DEVICE_ADDR_CTL_DIV2_Msk: 0UL);

    if(NULL != read)
    {
        SMIF_DEVICE_RD_CMD_CTL(dev) = (CY_SMIF_NO_COMMAND_OR_MODE != read->command) ?
                                    (_VAL2FLD(SMIF_DEVICE_RD_CMD_CTL_CODE,  (uint32_t)read->command)  |
                                    _VAL2FLD(SMIF_DEVICE_RD_CMD_CTL_WIDTH, (uint32_t)read->cmdWidth) |
                                    SMIF_DEVICE_RD_CMD_CTL_PRESENT_Msk)
                                    : 0U;

        SMIF_DEVICE_RD_ADDR_CTL(dev) = _VAL2FLD(SMIF_DEVICE_RD_ADDR_CTL_WIDTH, (uint32_t)read->addrWidth);

        SMIF_DEVICE_RD_MODE_CTL(dev) = (CY_SMIF_NO_COMMAND_OR_MODE != read->mode) ?
                                    (_VAL2FLD(SMIF_DEVICE_RD_CMD_CTL_CODE,  (uint32_t)read->mode)     |
                                        _VAL2FLD(SMIF_DEVICE_RD_CMD_CTL_WIDTH, (uint32_t)read->modeWidth)|
                                        SMIF_DEVICE_RD_CMD_CTL_PRESENT_Msk)
                                    : 0U;

        SMIF_DEVICE_RD_DUMMY_CTL(dev) = (0UL != read->dummyCycles)?
                                        (_VAL2FLD(SMIF_DEVICE_RD_DUMMY_CTL_SIZE5, (read->dummyCycles - 1UL)) |
                                        SMIF_DEVICE_RD_DUMMY_CTL_PRESENT_Msk)
                                        : 0U;

        SMIF_DEVICE_RD_DATA_CTL(dev) = _VAL2FLD(SMIF_DEVICE_RD_DATA_CTL_WIDTH, (uint32_t)read->dataWidth);
    }

    if(NULL != prog)
    {
        SMIF_DEVICE_WR_CMD_CTL(dev) = (CY_SMIF_NO_COMMAND_OR_MODE != prog->command) ?
                                    (_VAL2FLD(SMIF_DEVICE_WR_CMD_CTL_CODE,  (uint32_t)prog->command) |
                                    _VAL2FLD(SMIF_DEVICE_WR_CMD_CTL_WIDTH, (uint32_t)prog->cmdWidth)|
                                    SMIF_DEVICE_WR_CMD_CTL_PRESENT_Msk)
                                    : 0U;

        SMIF_DEVICE_WR_ADDR_CTL(dev) = _VAL2FLD(SMIF_DEVICE_WR_ADDR_CTL_WIDTH, (uint32_t)prog->addrWidth);

        SMIF_DEVICE_WR_MODE_CTL(dev) = (CY_SMIF_NO_COMMAND_OR_MODE != prog->mode) ?
                                        (_VAL2FLD(SMIF_DEVICE_WR_CMD_CTL_CODE,  (uint32_t)prog->mode)     |
                                        _VAL2FLD(SMIF_DEVICE_WR_CMD_CTL_WIDTH, (uint32_t)prog->modeWidth)|
                                        SMIF_DEVICE_WR_CMD_CTL_PRESENT_Msk)
                                        : 0UL;

        SMIF_DEVICE_WR_DUMMY_CTL(dev) = (0UL != prog->dummyCycles) ?
                                        (_VAL2FLD(SMIF_DEVICE_WR_DUMMY_CTL_SIZE5, (prog->dummyCycles - 1UL)) |
                                        SMIF_DEVICE_WR_DUMMY_CTL_PRESENT_Msk)
                                        : 0U;

        SMIF_DEVICE_WR_DATA_CTL(dev) = _VAL2FLD(SMIF_DEVICE_WR_DATA_CTL_WIDTH, (uint32_t)prog->dataWidth);
    }
}


/*******************************************************************************
* Function Name: Cy_SMIF_MemoryDeInit
****************************************************************************//**
*
* This function de-initializes all slave slots of the memory device to their default
* values.
*
* \param base
* Holds the base address of the SMIF block registers.
*
*******************************************************************************/
void Cy_SMIF_Memslot_DeInit(SMIF_Type *base)
{
    /* Configure the SMIF device slots to the default values. The default value is 0 */
    uint32_t deviceIndex;

    for(deviceIndex = 0UL; deviceIndex < (uint32_t)SMIF_DEVICE_NR; deviceIndex++)
    {
        SMIF_DEVICE_IDX_CTL(base, deviceIndex)           = 0U;
        SMIF_DEVICE_IDX_ADDR(base, deviceIndex)          = 0U;
        SMIF_DEVICE_IDX_MASK(base, deviceIndex)          = 0U;
        SMIF_DEVICE_IDX_ADDR_CTL(base, deviceIndex)      = 0U;
        SMIF_DEVICE_IDX_RD_CMD_CTL(base, deviceIndex)    = 0U;
        SMIF_DEVICE_IDX_RD_ADDR_CTL(base, deviceIndex)   = 0U;
        SMIF_DEVICE_IDX_RD_MODE_CTL(base, deviceIndex)   = 0U;
        SMIF_DEVICE_IDX_RD_DUMMY_CTL(base, deviceIndex)  = 0U;
        SMIF_DEVICE_IDX_RD_DATA_CTL(base, deviceIndex)   = 0U;
        SMIF_DEVICE_IDX_WR_CMD_CTL(base, deviceIndex)    = 0U;
        SMIF_DEVICE_IDX_WR_ADDR_CTL(base, deviceIndex)   = 0U;
        SMIF_DEVICE_IDX_WR_MODE_CTL(base, deviceIndex)   = 0U;
        SMIF_DEVICE_IDX_WR_DUMMY_CTL(base, deviceIndex)  = 0U;
        SMIF_DEVICE_IDX_WR_DATA_CTL(base, deviceIndex)   = 0U;
    }
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdWriteEnable
****************************************************************************//**
*
* This function sends the Write Enable command to the memory device.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* The Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode,
* this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param context
* The internal SMIF context data. \ref cy_stc_smif_context_t
*
* \return A status of the command transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdWriteEnable(SMIF_Type *base,
                                        cy_stc_smif_mem_config_t const *memDevice,
                                        cy_stc_smif_context_t const *context)
{
    /* The memory Write Enable */
    cy_stc_smif_mem_cmd_t* writeEn = memDevice->deviceCfg->writeEnCmd;
    
    cy_en_smif_status_t result = CY_SMIF_CMD_NOT_FOUND;
    
    if(NULL != writeEn)
    {
        result = Cy_SMIF_TransmitCommand( base, (uint8_t) writeEn->command,
                                        writeEn->cmdWidth,
                                        CY_SMIF_CMD_WITHOUT_PARAM,
                                        CY_SMIF_CMD_WITHOUT_PARAM,
                                        CY_SMIF_WIDTH_NA,
                                        memDevice->slaveSelect,
                                        CY_SMIF_TX_LAST_BYTE,
                                        context);
    }  

    return result;
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdWriteDisable
****************************************************************************//**
*
* This function sends a Write Disable command to the memory device.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode
* this API should be called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param context
* The internal SMIF context data. \ref cy_stc_smif_context_t
*
* \return A status of the command transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdWriteDisable(SMIF_Type *base,
                                    cy_stc_smif_mem_config_t const *memDevice,
                                    cy_stc_smif_context_t const *context)
{
    cy_stc_smif_mem_cmd_t* writeDis = memDevice->deviceCfg->writeDisCmd;

    cy_en_smif_status_t result = CY_SMIF_CMD_NOT_FOUND;
    
    if(NULL != writeDis)
    {
        /* The memory write disable */
        result = Cy_SMIF_TransmitCommand( base, (uint8_t)writeDis->command,
                                          writeDis->cmdWidth,
                                          CY_SMIF_CMD_WITHOUT_PARAM,
                                          CY_SMIF_CMD_WITHOUT_PARAM,
                                          CY_SMIF_WIDTH_NA,
                                          memDevice->slaveSelect,
                                          CY_SMIF_TX_LAST_BYTE,
                                          context);
    }
    
    return result;
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_IsBusy
****************************************************************************//**
*
* This function checks if the status of the memory device is busy.
* This is done by reading the status register and the corresponding bit
* (stsRegBusyMask). This function is a blocking function until the status
* register from the memory is read.
*
* \note In the dual quad mode, this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
*  The device to which the command is sent.
*
* \param context
* The internal SMIF context data.
*
* \return A status of the memory device.
*       - True - The device is busy or a timeout occurs.
*       - False - The device is not busy.
*
* \note Check \ref group_smif_usage_rules for any usage restriction 
*
*******************************************************************************/
bool Cy_SMIF_Memslot_IsBusy(SMIF_Type *base, cy_stc_smif_mem_config_t *memDevice,
                            cy_stc_smif_context_t const *context)
{
    uint8_t  status = 1U;
    cy_en_smif_status_t readStsResult = CY_SMIF_CMD_NOT_FOUND;
    cy_stc_smif_mem_device_cfg_t* device =  memDevice->deviceCfg;

    if(NULL != device->readStsRegWipCmd)
    {
        readStsResult = Cy_SMIF_Memslot_CmdReadSts(base, memDevice, &status,
                            (uint8_t)device->readStsRegWipCmd->command,
                            context);
    }  

    if (CY_SMIF_SUCCESS == readStsResult)
    {
        /* Masked not busy bits in returned status */
        status &= (uint8_t)device->stsRegBusyMask;
    }

    return (0U != status);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_QuadEnable
****************************************************************************//**
*
* This function enables the memory device for the quad mode of operation.
* This command must be executed before sending Quad SPI commands to the
* memory device.
*
* \note In the dual quad mode, this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param context
* The internal SMIF context data.
*
* \return A status of the command.
*   - \ref CY_SMIF_SUCCESS
*   - \ref CY_SMIF_NO_QE_BIT
*   - \ref CY_SMIF_CMD_FIFO_FULL
*   - \ref CY_SMIF_BAD_PARAM
*   - \ref CY_SMIF_CMD_NOT_FOUND
*
* \note Check \ref group_smif_usage_rules for any usage restriction 
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_QuadEnable(SMIF_Type *base,
                                    cy_stc_smif_mem_config_t *memDevice,
                                    cy_stc_smif_context_t const *context)
{
    cy_en_smif_status_t result= CY_SMIF_CMD_NOT_FOUND;
    uint8_t statusReg[CY_SMIF_QE_BIT_STS_REG2_T1] = {0U};  
    cy_stc_smif_mem_device_cfg_t* device =  memDevice->deviceCfg;

    /* Check that command exists */
    if((NULL != device->readStsRegQeCmd)  &&
       (NULL != device->writeStsRegQeCmd) &&
       (NULL != device->readStsRegWipCmd))
    {
        uint8_t readQeCmd  = (uint8_t)device->readStsRegQeCmd->command;
        uint8_t writeQeCmd = (uint8_t)device->writeStsRegQeCmd->command;
        uint8_t readWipCmd = (uint8_t)device->readStsRegWipCmd->command;

        result = Cy_SMIF_Memslot_CmdReadSts(base, memDevice, &statusReg[0U],
                    readQeCmd, context);
        if (CY_SMIF_SUCCESS == result)
        {
            uint32_t qeMask = device->stsRegQuadEnableMask;

            switch(qeMask)
            {
                case CY_SMIF_SFDP_QE_BIT_6_OF_SR_1:
                    statusReg[0U] |= (uint8_t)qeMask;
                    result = Cy_SMIF_Memslot_CmdWriteSts(base, memDevice,
                                &statusReg[0U], writeQeCmd, context);
                    break;
                case CY_SMIF_SFDP_QE_BIT_1_OF_SR_2:
                    /* Read status register 1 with the assumption that WIP is always in
                    * status register 1 */
                    result = Cy_SMIF_Memslot_CmdReadSts(base, memDevice,
                                &statusReg[0U], readWipCmd, context);
                    if (CY_SMIF_SUCCESS == result)
                    {
                        result = Cy_SMIF_Memslot_CmdReadSts(base, memDevice,
                                    &statusReg[1U], readQeCmd, context);
                        if (CY_SMIF_SUCCESS == result)
                        {
                            statusReg[1U] |= (uint8_t)qeMask;
                            result = Cy_SMIF_Memslot_CmdWriteSts(base, memDevice,
                                        statusReg, writeQeCmd, context);
                        }
                    }
                    break;
                case CY_SMIF_SFDP_QE_BIT_7_OF_SR_2:
                    result = Cy_SMIF_Memslot_CmdReadSts(base, memDevice,
                                &statusReg[1U], readQeCmd, context);
                    if (CY_SMIF_SUCCESS == result)
                    {
                        statusReg[1U] |= (uint8_t)qeMask;
                        result = Cy_SMIF_Memslot_CmdWriteSts(base, memDevice,
                                    &statusReg[1U], writeQeCmd, context);
                    }
                    break;
                default:
                    result = CY_SMIF_NO_QE_BIT;
                    break;
            }
        }
    }
    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdReadSts
****************************************************************************//**
*
* This function reads the status register. This function is a blocking function,
* it will block the execution flow until the status register is read.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* the Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode,
* this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param status
* The status register value returned by the external memory.
*
* \param command
* The command required to read the status/configuration register.
*
* \param context
* The internal SMIF context data.
*
* \return A status of the command reception.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_FIFO_FULL
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
* \note Check \ref group_smif_usage_rules for any usage restriction 
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdReadSts(SMIF_Type *base,
                                    cy_stc_smif_mem_config_t const *memDevice,
                                    uint8_t *status,
                                    uint8_t command,
                                    cy_stc_smif_context_t const *context)
{
    cy_en_smif_status_t result = CY_SMIF_CMD_NOT_FOUND;

    /* Read the memory status register */
    result = Cy_SMIF_TransmitCommand( base, command, CY_SMIF_WIDTH_SINGLE,
                CY_SMIF_CMD_WITHOUT_PARAM, CY_SMIF_CMD_WITHOUT_PARAM,
                CY_SMIF_WIDTH_NA, memDevice->slaveSelect, 
                CY_SMIF_TX_NOT_LAST_BYTE, context);

    if (CY_SMIF_SUCCESS == result)
    {
        result = Cy_SMIF_ReceiveDataBlocking( base, status,
                    CY_SMIF_READ_ONE_BYTE, CY_SMIF_WIDTH_SINGLE, context);
    }

    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdWriteSts
****************************************************************************//**
*
* This function writes the status register. This is a blocking function, it will
* block the execution flow until the command transmission is completed.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* The Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode,
* this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param status
* The status to write into the status register.
*
* \param command
* The command to write into the status/configuration register.
*
* \param context
* The internal SMIF context data. \ref cy_stc_smif_context_t              
*
* \return A status of the command transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdWriteSts(SMIF_Type *base,
                                    cy_stc_smif_mem_config_t const *memDevice,
                                    void const *status,
                                    uint8_t command,
                                    cy_stc_smif_context_t const *context)
{
    cy_en_smif_status_t result;

    /* The Write Enable */
    result =  Cy_SMIF_Memslot_CmdWriteEnable(base, memDevice, context);

    /* The Write Status */
    if (CY_SMIF_SUCCESS == result)
    {
        uint32_t size;
        uint32_t qeMask = memDevice->deviceCfg->stsRegQuadEnableMask;

        size = ( CY_SMIF_SFDP_QE_BIT_1_OF_SR_2 == qeMask)? CY_SMIF_WRITE_TWO_BYTES:
                                                        CY_SMIF_WRITE_ONE_BYTE;
        result = Cy_SMIF_TransmitCommand( base, command, CY_SMIF_WIDTH_SINGLE,
                    (uint8_t const *)status, size, CY_SMIF_WIDTH_SINGLE,
                    memDevice->slaveSelect, CY_SMIF_TX_LAST_BYTE, context);
    }

    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdChipErase
****************************************************************************//**
*
* This function performs a chip erase of the external memory. The Write Enable
* command is called before this API.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode,
* this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent
*
* \param context
* The internal SMIF context data. \ref cy_stc_smif_context_t
*
* \return A status of the command transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdChipErase(SMIF_Type *base,
                cy_stc_smif_mem_config_t const *memDevice,
                cy_stc_smif_context_t const *context)
{
    cy_en_smif_status_t result= CY_SMIF_CMD_NOT_FOUND;

    cy_stc_smif_mem_cmd_t *cmdErase = memDevice->deviceCfg->chipEraseCmd;
    if(NULL != cmdErase)
    {
        result = Cy_SMIF_TransmitCommand( base, (uint8_t)cmdErase->command,
                cmdErase->cmdWidth, CY_SMIF_CMD_WITHOUT_PARAM,
                CY_SMIF_CMD_WITHOUT_PARAM, CY_SMIF_WIDTH_NA,
                memDevice->slaveSelect, CY_SMIF_TX_LAST_BYTE, context);
    }

    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdSectorErase
****************************************************************************//**
*
* This function performs a block Erase of the external memory. The Write Enable
* command is called before this API.
*
* \note This function uses the low-level Cy_SMIF_TransmitCommand() API.
* The Cy_SMIF_TransmitCommand() API works in a blocking mode. In the dual quad mode,
* this API is called for each memory.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param sectorAddr
* The sector address to erase.
*
* \param context
* The internal SMIF context data. \ref cy_stc_smif_context_t
*
* \return A status of the command transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_BAD_PARAM
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdSectorErase(SMIF_Type *base,
                                        cy_stc_smif_mem_config_t *memDevice,
                                        uint8_t const *sectorAddr,
                                        cy_stc_smif_context_t const *context)
{
    cy_en_smif_status_t result = CY_SMIF_BAD_PARAM;

    if (NULL != sectorAddr)
    {

        cy_stc_smif_mem_device_cfg_t *device = memDevice->deviceCfg;
        cy_stc_smif_mem_cmd_t *cmdErase = device->eraseCmd;
        
        if(NULL != cmdErase)
        {
            result = Cy_SMIF_TransmitCommand( base, (uint8_t)cmdErase->command,
                    cmdErase->cmdWidth, sectorAddr, device->numOfAddrBytes,
                    cmdErase->cmdWidth, memDevice->slaveSelect,
                    CY_SMIF_TX_LAST_BYTE, context);
        }
        else
        {
            result = CY_SMIF_CMD_NOT_FOUND;
        }
    }
    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdProgram
****************************************************************************//**
*
* This function performs the Program operation. 
*
* \note This function uses the  Cy_SMIF_TransmitCommand() API.
* The Cy_SMIF_TransmitCommand() API works in the blocking mode. In the dual quad mode,
* this API works with both types of memory simultaneously.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param addr
* The address to program.
*
* \param writeBuff
* The pointer to the data to program. If this pointer is a NULL, then the
* function does not enable the interrupt. This use case is  typically used when
* the FIFO is handled outside the interrupt and is managed in either a 
* polling-based code or a DMA. The user would handle the FIFO management 
* in a DMA or a polling-based code. 
* If the user provides a NULL pointer in this function and does not handle 
* the FIFO transaction, this could either stall or timeout the operation 
* \ref Cy_SMIF_TransmitData().
*
* \param size
* The size of data to program. The user must ensure that the data size
* does not exceed the page size.
*
* \param cmdCmpltCb
* The callback function to call after the transfer completion. NULL interpreted
* as no callback.
*
* \param context
* The internal SMIF context data.
*
* \return A status of a transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_FIFO_FULL
*       - \ref CY_SMIF_BAD_PARAM
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdProgram(SMIF_Type *base,
                                    cy_stc_smif_mem_config_t const *memDevice,
                                    uint8_t const *addr,
                                    uint8_t* writeBuff,
                                    uint32_t size,
                                    cy_smif_event_cb_t cmdCmpltCb,
                                    cy_stc_smif_context_t *context)
{
    cy_en_smif_status_t result = CY_SMIF_SUCCESS;
    cy_en_smif_slave_select_t slaveSelected;

    cy_stc_smif_mem_device_cfg_t *device = memDevice->deviceCfg;
    cy_stc_smif_mem_cmd_t *cmdProg = device->programCmd;
    
    if(NULL == cmdProg)
    {
        result = CY_SMIF_CMD_NOT_FOUND;
    }
    else if ((NULL == addr) || (size > device->programSize))
    {
        result = CY_SMIF_BAD_PARAM;
    }
    else
    {
        slaveSelected = (0U == memDevice->dualQuadSlots)?  memDevice->slaveSelect :
                                                        (cy_en_smif_slave_select_t)memDevice->dualQuadSlots;
        /* The page program command */
        result = Cy_SMIF_TransmitCommand( base, (uint8_t)cmdProg->command,
                cmdProg->cmdWidth, addr, device->numOfAddrBytes,
                cmdProg->addrWidth, slaveSelected, CY_SMIF_TX_NOT_LAST_BYTE,
                context);

        if((CY_SMIF_SUCCESS == result) && (CY_SMIF_NO_COMMAND_OR_MODE != cmdProg->mode))
        {
            result = Cy_SMIF_TransmitCommand(base, (uint8_t)cmdProg->mode,
                        cmdProg->modeWidth, CY_SMIF_CMD_WITHOUT_PARAM,
                        CY_SMIF_CMD_WITHOUT_PARAM, CY_SMIF_WIDTH_NA,
                        (cy_en_smif_slave_select_t)slaveSelected,
                        CY_SMIF_TX_NOT_LAST_BYTE, context);
        }

        if((CY_SMIF_SUCCESS == result) && (cmdProg->dummyCycles > 0U))
        {
            result = Cy_SMIF_SendDummyCycles(base, cmdProg->dummyCycles);
        }

        if(CY_SMIF_SUCCESS == result)
        {
            result = Cy_SMIF_TransmitData( base, writeBuff, size,
                    cmdProg->dataWidth, cmdCmpltCb, context);
        }
    }

    return(result);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_CmdRead
****************************************************************************//**
*
* This function performs the Read operation.
*
* \note This function uses the Cy_SMIF_TransmitCommand() API.
* The Cy_SMIF_TransmitCommand() API works in the blocking mode. In the dual quad mode,
* this API works with both types of memory simultaneously.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param memDevice
* The device to which the command is sent.
*
* \param addr
* The address to read.
*
* \param readBuff
* The pointer to the variable where the read data is stored. If this pointer is 
* a NULL, then the function does not enable the interrupt. This use case is 
* typically used when the FIFO is handled outside the interrupt and is managed
* in either a  polling-based code or a DMA. The user would handle the FIFO
* management in a DMA or a polling-based code. 
* If the user provides a NULL pointer in this function and does not handle 
* the FIFO transaction, this could either stall or timeout the operation 
* \ref Cy_SMIF_TransmitData().
*
* \param size
* The size of data to read.
*
* \param cmdCmpltCb
* The callback function to call after the transfer completion. NULL interpreted
* as no callback.
*
* \param context
* The internal SMIF context data.
*
* \return A status of the transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_FIFO_FULL
*       - \ref CY_SMIF_BAD_PARAM
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
* \note Check \ref group_smif_usage_rules for any usage restriction 
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_CmdRead(SMIF_Type *base,
                                cy_stc_smif_mem_config_t const *memDevice,
                                uint8_t const *addr,
                                uint8_t* readBuff,
                                uint32_t size,
                                cy_smif_event_cb_t cmdCmpltCb,
                                cy_stc_smif_context_t *context)
{
    cy_en_smif_status_t result = CY_SMIF_BAD_PARAM;
    cy_en_smif_slave_select_t slaveSelected;
    cy_stc_smif_mem_device_cfg_t *device = memDevice->deviceCfg;
    cy_stc_smif_mem_cmd_t *cmdRead = device->readCmd;

    if(NULL == cmdRead)
    {
        result = CY_SMIF_CMD_NOT_FOUND;
    }
    else if(NULL == addr)
    {
        result = CY_SMIF_BAD_PARAM;
    }
    else
    {
        slaveSelected = (0U == memDevice->dualQuadSlots)?  memDevice->slaveSelect :
                               (cy_en_smif_slave_select_t)memDevice->dualQuadSlots;

        result = Cy_SMIF_TransmitCommand( base, (uint8_t)cmdRead->command,
                    cmdRead->cmdWidth, addr, device->numOfAddrBytes,
                    cmdRead->addrWidth, slaveSelected, CY_SMIF_TX_NOT_LAST_BYTE,
                    context);

        if((CY_SMIF_SUCCESS == result) && (CY_SMIF_NO_COMMAND_OR_MODE != cmdRead->mode))
        {
            result = Cy_SMIF_TransmitCommand(base, (uint8_t)cmdRead->mode,
                        cmdRead->modeWidth, CY_SMIF_CMD_WITHOUT_PARAM,
                        CY_SMIF_CMD_WITHOUT_PARAM, CY_SMIF_WIDTH_NA,
                        (cy_en_smif_slave_select_t)slaveSelected,
                        CY_SMIF_TX_NOT_LAST_BYTE, context);
        }

        if((CY_SMIF_SUCCESS == result) && (0U < cmdRead->dummyCycles))
        {
            result = Cy_SMIF_SendDummyCycles(base, cmdRead->dummyCycles);
        }

        if(CY_SMIF_SUCCESS == result)
        {
            result = Cy_SMIF_ReceiveData(base, readBuff, size,
                        cmdRead->dataWidth, cmdCmpltCb, context);
        }
    }

    return(result);
}


/*******************************************************************************
* Function Name: SfdpReadBuffer
****************************************************************************//**
*
* This function reads the tables in the SDFP database into the buffer.
*
* \note This function is a blocking function and blocks until the structure data
* is read and returned. This function uses \ref Cy_SMIF_TransmitCommand()
*
* \param *base
* Holds the base address of the SMIF block registers.
*
* \param *cmdSfdp
*  The command structure to store the Read/Write command
*  configuration.
*
* \param sfdpAddress
* The pointer to an array with the address bytes
* associated with the memory command.
*
* \param slaveSelect
* Denotes the number of the slave device to which the transfer is made.
* (0, 1, 2 or 4 - the bit defines which slave to enable). The two-bit enable
* is possible only for the Double Quad SPI mode.
*
* \param size
* The size of data to be received. Must be > 0 and not greater than 65536.
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param context
* Internal SMIF context data.
*
* \return A status of the transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_FIFO_FULL
*       - \ref CY_SMIF_NO_SFDP_SUPPORT
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*
*******************************************************************************/
static cy_en_smif_status_t SfdpReadBuffer(SMIF_Type *base, 
                                         cy_stc_smif_mem_cmd_t const *cmdSfdp,
                                         uint8_t const sfdpAddress[],
                                         cy_en_smif_slave_select_t  slaveSelect,
                                         uint32_t size,
                                         uint8_t sfdpBuffer[],
                                         cy_stc_smif_context_t *context)
{
    cy_en_smif_status_t result = CY_SMIF_NO_SFDP_SUPPORT;

    result = Cy_SMIF_TransmitCommand( base, (uint8_t)cmdSfdp->command,
                cmdSfdp->cmdWidth, sfdpAddress, CY_SMIF_SFDP_ADDRESS_LENGTH,
                cmdSfdp->addrWidth, slaveSelect, CY_SMIF_TX_NOT_LAST_BYTE,
                context);

    if(CY_SMIF_SUCCESS == result)
    {
        result = Cy_SMIF_SendDummyCycles(base, cmdSfdp->dummyCycles);
        
        /* Get data from SFDP and 1st Basic Flash Parameter Headers only */
        if(CY_SMIF_SUCCESS == result)
        {
            result = Cy_SMIF_ReceiveData( base, sfdpBuffer, size,
                                         cmdSfdp->dataWidth, NULL, context);
                                         
            if (CY_SMIF_SUCCESS == result)
            {
                uint32_t cmdTimeout = context->timeout;
                while (((uint32_t) CY_SMIF_REC_CMPLT != context->transferStatus) &&
                        (CY_SMIF_EXCEED_TIMEOUT != result))
                {
                    /* Wait until the Read of the SFDP operation is completed */
                    result = Cy_SMIF_TimeoutRun(&cmdTimeout);
                }
            }
        }   
    }

    return(result);
}


/*******************************************************************************
* Function Name: SfdpFindParameterHeader
****************************************************************************//**
*
* Finds the Parameter Header offset from the JEDEC basic flash parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param id
* The parameter ID.
*
* \return The relative parameter header offset in bytes.
*  Returns 0 when the parameter header is not found.
*
*******************************************************************************/
static uint32_t SfdpFindParameterHeader(uint32_t id, uint8_t const sfdpBuffer[])
{
    uint32_t headerOffset = PARAMETER_IS_NOT_FOUND;
    uint32_t maxMinorRevison = 0UL;
    uint32_t sfdpAddress = FIRST_HEADER_OFFSET; /* Begin from 1st Parameter Header */
    
    while (sfdpAddress <= (((uint32_t)sfdpBuffer[PARAM_HEADERS_NUM] * 
                                        HEADER_LENGTH) +
                                        FIRST_HEADER_OFFSET))
    {
        /* Check parameter ID */
        if (((id & PARAM_ID_LSB_MASK) == sfdpBuffer[sfdpAddress]) &&  /* Parameter ID LSB */ 
            (((id >> PARAM_ID_MSB_OFFSET) & PARAM_ID_LSB_MASK) == 
                    sfdpBuffer[sfdpAddress +  /* Parameter ID MSB */
                    PARAM_ID_MSB_REL_OFFSET]))
        {       
            /* Check parameter major and minor revisions */        
            if ((sfdpBuffer[sfdpAddress + PARAM_MINOR_REV_REL_OFFSET] >= maxMinorRevison) &&
                (sfdpBuffer[sfdpAddress + PARAM_MAJOR_REV_REL_OFFSET] == CY_SMIF_SFDP_MAJOR_REV_1))
            {
                /* Get the maximum minor revision */   
                maxMinorRevison = sfdpBuffer[sfdpAddress + PARAM_MINOR_REV_REL_OFFSET];
                
                /* Save the the Parameter Header offset with the maximum minor revision */
                headerOffset = sfdpAddress;
            } 
        }
    
        sfdpAddress += HEADER_LENGTH;       
    }

    return(headerOffset);
}


/*******************************************************************************
* Function Name: SfdpFindParameterTableAddress
****************************************************************************//**
*
* Reads the address and length of the Parameter Table from 
* the JEDEC basic flash parameter table. 
*
* \param id
* The parameter ID.
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param address
* The Parameter Table address.
*
* \param *tableLength
* The Parameter Table length.
*
* \return The command reception status.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_NOT_FOUND
*
*******************************************************************************/
static cy_en_smif_status_t SfdpFindParameterTableAddress(uint32_t id,
                                                    uint8_t const sfdpBuffer[],
                                                    uint8_t address[],
                                                    uint32_t *tableLength)
{
    cy_en_smif_status_t result = CY_SMIF_CMD_NOT_FOUND;
    uint32_t headerOffset;
    
    headerOffset = SfdpFindParameterHeader(id, sfdpBuffer);

    if (PARAMETER_IS_NOT_FOUND != headerOffset)
    {
        /* The Parameter Table address */
        address[2] = sfdpBuffer[headerOffset +
                                PARAM_TABLE_PRT_OFFSET];
        address[1] = sfdpBuffer[headerOffset +
                                PARAM_TABLE_PRT_OFFSET + 1UL];
        address[0] = sfdpBuffer[headerOffset +
                                PARAM_TABLE_PRT_OFFSET + 2UL];

        /* The Parameter Table length */
        *tableLength = (uint32_t)sfdpBuffer[headerOffset + PARAM_TABLE_LENGTH_OFFSET] * 
                       BYTES_IN_DWORD;
                       
        result = CY_SMIF_SUCCESS;              
    }
 
    return(result);
}


/*******************************************************************************
* Function Name: SfdpGetNumOfAddrBytes
****************************************************************************//**
*
* Reads the number of address bytes from the JEDEC basic flash parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return The number of address bytes used by the memory slave device.
*
*******************************************************************************/
static uint32_t SfdpGetNumOfAddrBytes(uint8_t const sfdpBuffer[])
{
    uint32_t addrBytesNum = 0UL;
    uint32_t sfdpAddrCode = _FLD2VAL(CY_SMIF_SFDP_ADDRESS_BYTES,
                                        (uint32_t)sfdpBuffer
                                        [CY_SMIF_SFDP_BFPT_BYTE_02]);
    switch(sfdpAddrCode)
    {
        case CY_SMIF_SFDP_THREE_BYTES_ADDR_CODE:
            addrBytesNum = CY_SMIF_THREE_BYTES_ADDR;
            break;
        case CY_SMIF_SFDP_THREE_OR_FOUR_BYTES_ADDR_CODE:
            addrBytesNum = CY_SMIF_THREE_BYTES_ADDR;
            break;
        case CY_SMIF_SFDP_FOUR_BYTES_ADDR_CODE:
            addrBytesNum = CY_SMIF_FOUR_BYTES_ADDR;
            break;
        default:
            break;
    }
            
    return(addrBytesNum);
}


/*******************************************************************************
* Function Name: SfdpGetMemoryDensity
****************************************************************************//**
*
* Reads the Memory Density from the JEDEC basic flash parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return The external memory size:
* For densities of 2 gigabits or less - the size in bytes;
* For densities 4 gigabits and above - bit-31 is set to 1b to define that
* this memory is 4 gigabits and above; and other 30:0 bits define N where 
* the density is computed as 2^N bytes. 
* For example, 0x80000021 corresponds to 2^30 = 1 gigabyte. 
*
*******************************************************************************/
static uint32_t SfdpGetMemoryDensity(uint8_t const sfdpBuffer[])
{
    uint32_t memorySize;
    uint32_t locSize = Cy_SMIF_PackBytesArray(&sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_04], true);

    if (0UL == (locSize & CY_SMIF_SFDP_SIZE_ABOVE_4GB_Msk))
    {
        memorySize = (locSize + 1UL)/BITS_IN_BYTE;
    }
    else
    {
        memorySize = (locSize - BITS_IN_BYTE_ABOVE_4GB) |
                CY_SMIF_SFDP_SIZE_ABOVE_4GB_Msk;
    }
                
    return(memorySize);
}


/*******************************************************************************
* Function Name: SfdpGetReadCmd_1_4_4
****************************************************************************//**
*
* Reads the FAST_READ_1_4_4 read command parameters from the JEDEC basic flash 
* parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmd_1_4_4(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    /* 8-bit command. 4 x I/O Read command */
    cmdRead->command = sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_09];

    /* The command transfer width */
    cmdRead->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdRead->addrWidth = CY_SMIF_WIDTH_QUAD;

    /* The 8-bit mode byte. This value is 0xFFFFFFFF when there is no mode present */
    if (0U == (_FLD2VAL(CY_SMIF_SFDP_1_4_4_MODE_CYCLES,
               (uint32_t) sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_08])))
    {
        cmdRead->mode = CY_SMIF_NO_COMMAND_OR_MODE;
    }
    else
    {
        cmdRead->mode = READ_ENHANCED_MODE_DISABLED;
        cmdRead->modeWidth = CY_SMIF_WIDTH_QUAD;
    }

    /* The dummy cycles number. A zero value suggests no dummy cycles */
    cmdRead->dummyCycles = _FLD2VAL(CY_SMIF_SFDP_1_4_4_DUMMY_CYCLES, 
                           (uint32_t) sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_08]);

    /* The data transfer width */
    cmdRead->dataWidth = CY_SMIF_WIDTH_QUAD;    
}


/*******************************************************************************
* Function Name: SfdpGetReadCmd_1_1_4
****************************************************************************//**
*
* Reads the FAST_READ_1_1_4 read command parameters from the JEDEC basic flash 
* parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmd_1_1_4(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    /* 8-bit command. 4 x I/O Read command */
    cmdRead->command  =
            sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0B];

    /* The command transfer width */
    cmdRead->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdRead->addrWidth = CY_SMIF_WIDTH_SINGLE;

    /* The 8-bit mode byte. This value is 0xFFFFFFFF when there is no mode present */
    if ((0U == _FLD2VAL(CY_SMIF_SFDP_1_1_4_MODE_CYCLES, (uint32_t) sfdpBuffer
                    [CY_SMIF_SFDP_BFPT_BYTE_0A])))
    {
        cmdRead->mode = CY_SMIF_NO_COMMAND_OR_MODE;
    }
    else
    {
        cmdRead->mode = READ_ENHANCED_MODE_DISABLED;
        cmdRead->modeWidth = CY_SMIF_WIDTH_SINGLE;
    }

    /* The dummy cycles number. A zero value suggests no dummy cycles */
    cmdRead->dummyCycles = _FLD2VAL(CY_SMIF_SFDP_1_1_4_DUMMY_CYCLES,
                          (uint32_t)sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0A]);

    /* The data transfer width */
    cmdRead->dataWidth = CY_SMIF_WIDTH_QUAD;  
}


/*******************************************************************************
* Function Name: SfdpGetReadCmd_1_2_2
****************************************************************************//**
*
* Reads the FAST_READ_1_2_2 read command parameters from the JEDEC basic flash 
* parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmd_1_2_2(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    /* 8-bit command. 2 x I/O Read command */
    cmdRead->command  = sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0F];

    /* The command transfer width */
    cmdRead->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdRead->addrWidth = CY_SMIF_WIDTH_DUAL;

    /* The 8-bit mode byte. This value is 0xFFFFFFFF when there is no mode present */
    if (0U == _FLD2VAL(CY_SMIF_SFDP_1_2_2_MODE_CYCLES, (uint32_t)
              sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0E]))
    {
        cmdRead->mode = CY_SMIF_NO_COMMAND_OR_MODE;
    }
    else
    {
        cmdRead->mode = READ_ENHANCED_MODE_DISABLED;
        cmdRead->modeWidth = CY_SMIF_WIDTH_DUAL;
    }

    /* The dummy cycles number. A zero value suggests no dummy cycles. */
    cmdRead->dummyCycles = _FLD2VAL(CY_SMIF_SFDP_1_2_2_DUMMY_CYCLES,
                            (uint32_t) sfdpBuffer [CY_SMIF_SFDP_BFPT_BYTE_0E]);

    /* The data transfer width */
    cmdRead->dataWidth = CY_SMIF_WIDTH_DUAL;
}


/*******************************************************************************
* Function Name: SfdpGetReadCmd_1_1_2
****************************************************************************//**
*
* Reads the FAST_READ_1_1_2 read command parameters from the JEDEC basic flash 
* parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmd_1_1_2(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    /* 8-bit command. 2 x I/O Read command */
    cmdRead->command  = sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0D];

    /* The command transfer width */
    cmdRead->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdRead->addrWidth = CY_SMIF_WIDTH_SINGLE;

    /* The 8-bit mode byte. This value is 0xFFFFFFFF when there is no mode present */
    if ((_FLD2VAL(CY_SMIF_SFDP_1_1_2_MODE_CYCLES, (uint32_t)
        sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0C])) == 0U)
    {
        cmdRead->mode = CY_SMIF_NO_COMMAND_OR_MODE;
    }
    else
    {
        cmdRead->mode = READ_ENHANCED_MODE_DISABLED;
        cmdRead->modeWidth = CY_SMIF_WIDTH_SINGLE;
    }

    /* The dummy cycles number. A zero value suggests no dummy cycles. */
    cmdRead->dummyCycles = _FLD2VAL(CY_SMIF_SFDP_1_1_2_DUMMY_CYCLES,
            (uint32_t)sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_0C]);

    /* The data transfer width */
    cmdRead->dataWidth = CY_SMIF_WIDTH_DUAL;
}


/*******************************************************************************
* Function Name: SfdpGetReadCmd_1_1_1
****************************************************************************//**
*
* Reads the FAST_READ_1_1_1 read command parameters from the JEDEC basic flash 
* parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmd_1_1_1(uint8_t const sfdpBuffer[], 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    /* 8-bit command. 1 x I/O Read command */
    cmdRead->command  = CY_SMIF_SINGLE_READ_CMD;

    /* The command transfer width */
    cmdRead->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdRead->addrWidth = CY_SMIF_WIDTH_SINGLE;

    /* The 8 bit-mode byte. This value is 0xFFFFFFFF when there is no mode present */
    cmdRead->mode = CY_SMIF_NO_COMMAND_OR_MODE;

    /* The dummy cycles number. A zero value suggests no dummy cycles. */
    cmdRead->dummyCycles = 0UL;

    /* The data transfer width */
    cmdRead->dataWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpGetReadCmdParams
****************************************************************************//**
*
* Reads the read command parameters from the JEDEC basic flash parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \param dataSelect
* The data line selection options for a slave device.
*
* \param cmdRead
* The pointer to the read command parameters structure.
*
*******************************************************************************/
static void SfdpGetReadCmdParams(uint8_t const sfdpBuffer[], 
                             cy_en_smif_data_select_t dataSelect, 
                             cy_stc_smif_mem_cmd_t* cmdRead)
{
    uint32_t sfdpDataIndex = CY_SMIF_SFDP_BFPT_BYTE_02;
    bool quadEnabled = ((CY_SMIF_DATA_SEL1 != dataSelect) &&
                        (CY_SMIF_DATA_SEL3 != dataSelect));

    if (quadEnabled)
    {
        if (1UL == _FLD2VAL(CY_SMIF_SFDP_FAST_READ_1_4_4,
                       ((uint32_t) sfdpBuffer[sfdpDataIndex])))
                       
        {
            SfdpGetReadCmd_1_4_4(sfdpBuffer, cmdRead);
        }
        else if (1UL == _FLD2VAL(CY_SMIF_SFDP_FAST_READ_1_1_4,
                            ((uint32_t)sfdpBuffer[sfdpDataIndex])))
        {
            SfdpGetReadCmd_1_1_4(sfdpBuffer, cmdRead);
        }
        else
        {
            /* Wrong mode */
            CY_ASSERT_L2(true);            
        }
    }
    else
    {
        if ((1UL == _FLD2VAL(CY_SMIF_SFDP_FAST_READ_1_2_2,
                            (uint32_t)sfdpBuffer[sfdpDataIndex])))
        {
            SfdpGetReadCmd_1_2_2(sfdpBuffer, cmdRead);
        }
        else
        {
            if (1UL == _FLD2VAL(CY_SMIF_SFDP_FAST_READ_1_1_2,
                                (uint32_t)sfdpBuffer[sfdpDataIndex]))
            {
                SfdpGetReadCmd_1_1_2(sfdpBuffer, cmdRead);
            }
            else
            {
                SfdpGetReadCmd_1_1_1(sfdpBuffer, cmdRead);
            }
        }
    }
}


/*******************************************************************************
* Function Name: SfdpGetPageSize
****************************************************************************//**
*
* Reads the page size from the JEDEC basic flash parameter table. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return The page size in bytes.
*
*******************************************************************************/
static uint32_t SfdpGetPageSize(uint8_t const sfdpBuffer[])
{
    uint32_t size;

    /* The page size */
    size = 0x01UL << _FLD2VAL(CY_SMIF_SFDP_PAGE_SIZE,
        (uint32_t) sfdpBuffer[CY_SMIF_SFDP_BFPT_BYTE_28]); 
                
    return(size);
}


/*******************************************************************************
* Function Name: SfdpGetEraseTime
****************************************************************************//**
*
* Calculates erase time. 
*
* \param eraseOffset
* The offset of the Sector Erase command in the SFDP buffer.
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return Erase time in us.
*
*******************************************************************************/
static uint32_t SfdpGetEraseTime(uint32_t const eraseOffset, uint8_t const sfdpBuffer[])
{
    /* Get the value of 10th DWORD from the JEDEC basic flash parameter table */
    uint32_t readEraseTime = ((uint32_t*)sfdpBuffer)[CY_SMIF_JEDEC_BFPT_10TH_DWORD];

    uint32_t eraseTimeMax;
    uint32_t eraseTimeIndex = (((eraseOffset - CY_SMIF_SFDP_BFPT_BYTE_1D) + TYPE_STEP) / TYPE_STEP);
    uint32_t eraseUnits = _FLD2VAL(ERASE_T_UNITS, 
                          (readEraseTime >> ((eraseTimeIndex - 1UL) * ERASE_T_LENGTH))
                           >> ERASE_T_COUNT_OFFSET);
    uint32_t eraseCount = _FLD2VAL(ERASE_T_COUNT, 
                          (readEraseTime >> ((eraseTimeIndex - 1UL) * ERASE_T_LENGTH)) 
                          >> ERASE_T_COUNT_OFFSET);
    uint32_t eraseMul = _FLD2VAL(CY_SMIF_SFDP_ERASE_MUL_COUNT, readEraseTime);
    uint32_t eraseMs = 0UL;

    switch (eraseUnits)
    {
        case CY_SMIF_SFDP_UNIT_0:
            eraseMs = CY_SMIF_SFDP_ERASE_TIME_1MS;
            break;
        case CY_SMIF_SFDP_UNIT_1:
            eraseMs = CY_SMIF_SFDP_ERASE_TIME_16MS;
            break;
        case CY_SMIF_SFDP_UNIT_2:
            eraseMs = CY_SMIF_SFDP_ERASE_TIME_128MS;
            break;
        case CY_SMIF_SFDP_UNIT_3:
            eraseMs = CY_SMIF_SFDP_ERASE_TIME_1S;
            break;
        default:
            /* An unsupported SFDP value */
            break;
    }

    /* Convert typical time to max time */
    eraseTimeMax = ((eraseCount + 1UL) * eraseMs) * (2UL * (eraseMul + 1UL));
                
    return(eraseTimeMax);
}


/*******************************************************************************
* Function Name: SfdpGetChipEraseTime
****************************************************************************//**
*
* Calculates chip erase time. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return Chip erase time in us.
*
*******************************************************************************/
static uint32_t SfdpGetChipEraseTime(uint8_t const sfdpBuffer[])
{
    /* Get the value of 10th DWORD from the JEDEC basic flash parameter table */
    uint32_t readEraseTime = ((uint32_t*)sfdpBuffer)[CY_SMIF_JEDEC_BFPT_10TH_DWORD];

    /* Get the value of 11th DWORD from the JEDEC basic flash parameter table */
    uint32_t chipEraseProgTime = ((uint32_t*)sfdpBuffer)[CY_SMIF_JEDEC_BFPT_11TH_DWORD];

    uint32_t chipEraseTimeMax;
    uint32_t chipEraseUnits = _FLD2VAL(CY_SMIF_SFDP_CHIP_ERASE_UNITS, chipEraseProgTime);
    uint32_t chipEraseCount = _FLD2VAL(CY_SMIF_SFDP_CHIP_ERASE_COUNT, chipEraseProgTime);
    uint32_t chipEraseMs = 0UL;
    uint32_t eraseMul = _FLD2VAL(CY_SMIF_SFDP_ERASE_MUL_COUNT, readEraseTime);

    switch (chipEraseUnits)
    {
        case CY_SMIF_SFDP_UNIT_0:
            chipEraseMs = CY_SMIF_SFDP_CHIP_ERASE_TIME_16MS;
            break;
        case CY_SMIF_SFDP_UNIT_1:
            chipEraseMs = CY_SMIF_SFDP_CHIP_ERASE_TIME_256MS;
            break;
        case CY_SMIF_SFDP_UNIT_2:
            chipEraseMs = CY_SMIF_SFDP_CHIP_ERASE_TIME_4S;
            break;
        case CY_SMIF_SFDP_UNIT_3:
            chipEraseMs = CY_SMIF_SFDP_CHIP_ERASE_TIME_64S;
            break;
        default:
            /* An unsupported SFDP value*/
            break;
    }

    /* Convert typical time to max time */
    chipEraseTimeMax = ((chipEraseCount + 1UL)*chipEraseMs) * (2UL *(eraseMul + 1UL)); 
                
    return(chipEraseTimeMax);
}


/*******************************************************************************
* Function Name: SfdpGetPageProgramTime
****************************************************************************//**
*
* Calculates page program time. 
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
* \return Page program time in us.
*
*******************************************************************************/
static uint32_t SfdpGetPageProgramTime(uint8_t const sfdpBuffer[])
{
    /* Get the value of 11th DWORD from the JEDEC basic flash parameter table */
    uint32_t chipEraseProgTime = ((uint32_t*)sfdpBuffer)[CY_SMIF_JEDEC_BFPT_11TH_DWORD];
    uint32_t programTimeMax;
    uint32_t programTimeUnits = _FLD2VAL(CY_SMIF_SFDP_PAGE_PROG_UNITS, chipEraseProgTime);
    uint32_t programTimeCount  = _FLD2VAL(CY_SMIF_SFDP_PAGE_PROG_COUNT, chipEraseProgTime);
    uint32_t progMul = _FLD2VAL(CY_SMIF_SFDP_PROG_MUL_COUNT, chipEraseProgTime);
    uint32_t progUs;
    
    if (CY_SMIF_SFDP_UNIT_0 == programTimeUnits)
    {
        progUs = CY_SMIF_SFDP_PROG_TIME_8US;
    }
    else
    {
        progUs = CY_SMIF_SFDP_PROG_TIME_64US;
    }

    /* Convert typical time to max time */
    programTimeMax = ((programTimeCount + 1UL) * progUs) * (2UL * (progMul + 1UL));
                
    return(programTimeMax);
}


/*******************************************************************************
* Function Name: SfdpSetWriteEnableCommand
****************************************************************************//**
*
* Sets the Write Enable command and the width of the command transfer.
*
* \param cmdWriteEnable
* The pointer to the Write Enable command parameters structure.
*
*******************************************************************************/
static void SfdpSetWriteEnableCommand(cy_stc_smif_mem_cmd_t* cmdWriteEnable)
{
    /* 8-bit command. Write Enable */
    cmdWriteEnable->command = CY_SMIF_WR_ENABLE_CMD;

    /* The width of the command transfer */
    cmdWriteEnable->cmdWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpSetWriteDisableCommand
****************************************************************************//**
*
* Sets the Write Disable command and the width of the command transfer.
*
* \param cmdWriteDisable
* The pointer to the Write Disable command parameters structure.
*
*******************************************************************************/
static void SfdpSetWriteDisableCommand(cy_stc_smif_mem_cmd_t* cmdWriteDisable)
{
    /* The 8-bit command. Write Disable */
    cmdWriteDisable->command = CY_SMIF_WR_DISABLE_CMD;

    /* The width of the command transfer */
    cmdWriteDisable->cmdWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpSetProgramCommand
****************************************************************************//**
*
* Sets the Program command parameters.
*
* \param cmdProgram
* The pointer to the Program command parameters structure.
*
*******************************************************************************/
static void SfdpSetProgramCommand(cy_stc_smif_mem_cmd_t* cmdProgram)
{
    /* 8-bit command. 1 x I/O Program command */
    cmdProgram->command = CY_SMIF_SINGLE_PROGRAM_CMD;
    /* The command transfer width */
    cmdProgram->cmdWidth = CY_SMIF_WIDTH_SINGLE;
    /* The address transfer width */
    cmdProgram->addrWidth = CY_SMIF_WIDTH_SINGLE;
    /* 8-bit mode byte. This value is 0xFFFFFFFF when there is no mode present */
    cmdProgram->mode = CY_SMIF_NO_COMMAND_OR_MODE;
    /* The dummy cycles number. A zero value suggests no dummy cycles */
    cmdProgram->dummyCycles = 0UL;
    /* The data transfer width */
    cmdProgram->dataWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpSetWipStatusRegisterCommand
****************************************************************************//**
*
* Sets the WIP-containing status register command and 
* the width of the command transfer.
*
* \param readStsRegWipCmd
* The pointer to the WIP-containing status register command parameters structure.
*
*******************************************************************************/
static void SfdpSetWipStatusRegisterCommand(cy_stc_smif_mem_cmd_t* readStsRegWipCmd)
{
    /* 8-bit command. WIP RDSR */
    readStsRegWipCmd->command  = CY_SMIF_RD_STS_REG1_CMD;
    /* The command transfer width */
    readStsRegWipCmd->cmdWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpGetQuadEnableParameters
****************************************************************************//**
*
* Gets the Quad Enable parameters.
*
* \param device
* The device structure instance declared by the user. This is where the detected
* parameters are stored and returned.
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
*******************************************************************************/
static void SfdpGetQuadEnableParameters(cy_stc_smif_mem_device_cfg_t *device, 
                                    uint8_t const sfdpBuffer[])
{
    /* The command transfer width */
    device->writeStsRegQeCmd->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The QE mask for the status registers */
    switch (_FLD2VAL(CY_SMIF_SFDP_QE_REQUIREMENTS, (uint32_t)sfdpBuffer
        [CY_SMIF_SFDP_BFPT_BYTE_3A]))
    {
        case CY_SMIF_SFDP_QER_0:
            device->stsRegQuadEnableMask = CY_SMIF_NO_COMMAND_OR_MODE;
            device->writeStsRegQeCmd->command  = CY_SMIF_NO_COMMAND_OR_MODE;
            device->readStsRegQeCmd->command  = CY_SMIF_NO_COMMAND_OR_MODE;
            break;
        case CY_SMIF_SFDP_QER_1:
        case CY_SMIF_SFDP_QER_4:
        case CY_SMIF_SFDP_QER_5:
            device->stsRegQuadEnableMask = CY_SMIF_SFDP_QE_BIT_1_OF_SR_2;

            /* The command to write into the QE-containing status register */
            /* The 8-bit command. QE WRSR */
            device->writeStsRegQeCmd->command  = CY_SMIF_WR_STS_REG1_CMD;
            device->readStsRegQeCmd->command  = CY_SMIF_RD_STS_REG2_T1_CMD;
            break;
        case CY_SMIF_SFDP_QER_2:
            device->stsRegQuadEnableMask = CY_SMIF_SFDP_QE_BIT_6_OF_SR_1;

            /* The command to write into the QE-containing status register */
            /* The 8-bit command. QE WRSR */
            device->writeStsRegQeCmd->command  = CY_SMIF_WR_STS_REG1_CMD;
            device->readStsRegQeCmd->command  = CY_SMIF_RD_STS_REG1_CMD;
            break;
        case CY_SMIF_SFDP_QER_3:
            device->stsRegQuadEnableMask = CY_SMIF_SFDP_QE_BIT_7_OF_SR_2;

            /* The command to write into the QE-containing status register */
            /* The 8-bit command. QE WRSR */
            device->writeStsRegQeCmd->command  = CY_SMIF_WR_STS_REG2_CMD;
            device->readStsRegQeCmd->command  = CY_SMIF_RD_STS_REG2_T2_CMD;
            break;
        default:
            break;
    }
}


/*******************************************************************************
* Function Name: SfdpSetChipEraseCommand
****************************************************************************//**
*
* Sets the Chip Erase command and the width of the command transfer.
*
* \param cmdChipErase
* The pointer to the Chip Erase command parameters structure.
*
* \param sfdpBuffer
* The pointer to an array with the SDFP buffer.
*
*******************************************************************************/
static void SfdpSetChipEraseCommand(cy_stc_smif_mem_cmd_t* cmdChipErase)
{
    /* 8-bit command. Chip Erase */
    cmdChipErase->command  = CY_SMIF_CHIP_ERASE_CMD;
    /* The width of the command transfer */
    cmdChipErase->cmdWidth = CY_SMIF_WIDTH_SINGLE;
}


/*******************************************************************************
* Function Name: SfdpGetSectorEraseCommand
****************************************************************************//**
*
* Sets the Sector Erase command and the width of the command transfer.
*
* \param cmdSectorErase
* The pointer to the Sector Erase command parameters structure.
*
* \return The offset of the Sector Erase command in the SFDP buffer. 
*  Returns 0 when the Sector Erase command is not found.
*
*******************************************************************************/
static uint32_t SfdpGetSectorEraseCommand(cy_stc_smif_mem_cmd_t* cmdSectorErase, 
                                   uint8_t const sfdpBuffer[])
{
    uint32_t eraseOffset = CY_SMIF_SFDP_BFPT_BYTE_1D;
    while (INSTRUCTION_NOT_SUPPORTED == sfdpBuffer[eraseOffset])
    {
        if (eraseOffset >= CY_SMIF_SFDP_BFPT_BYTE_23)
        {
            /* The Sector Erase command is not found */
            eraseOffset = COMMAND_IS_NOT_FOUND;
            break;
        }
        eraseOffset += TYPE_STEP; /* Check the next Erase Type */
    }
    
    /* Get the sector Erase command from the JEDEC basic flash parameter table */
    cmdSectorErase->command = sfdpBuffer[eraseOffset];  

    /* The command transfer width */
    cmdSectorErase->cmdWidth = CY_SMIF_WIDTH_SINGLE;

    /* The address transfer width */
    cmdSectorErase->addrWidth = CY_SMIF_WIDTH_SINGLE;
    
    return(eraseOffset);
}


/*******************************************************************************
* Function Name: Cy_SMIF_Memslot_SfdpDetect
****************************************************************************//**
*
* This function detects the device signature for SFDP devices.
* Refer to the SFDP spec (JESD216B) for details.
* The function asks the device using an SPI and then populates the relevant
* parameters for \ref cy_stc_smif_mem_device_cfg_t.
*
* \note This function is a blocking function and blocks until the structure data
* is read and returned. This function uses \ref Cy_SMIF_TransmitCommand()
* If there is no support for SFDP in the memory device, the API returns an
* error condition. The function requires:
*   - SMIF initialized and enabled to work in the normal mode;
*   - readSfdpCmd field of \ref cy_stc_smif_mem_device_cfg_t is enabled.
*
* \note The SFDP detect takes into account the types of the SPI supported by the
* memory device and also the dataSelect option selected to choose which SPI mode
* (SPI, DSPI, QSPI) to load into the structures. The algorithm prefers
* QSPI>DSPI>SPI, provided there is support for it in the memory device and the
* dataSelect selected by the user.
*
* \param base
* Holds the base address of the SMIF block registers.
*
* \param device
* The device structure instance declared by the user. This is where the detected
* parameters are stored and returned.
*
* \param slaveSelect
* The slave select line for the device.
*
* \param dataSelect
* The data line selection options for a slave device.
*
* \param context
* Internal SMIF context data.
*
* \return A status of the transmission.
*       - \ref CY_SMIF_SUCCESS
*       - \ref CY_SMIF_CMD_FIFO_FULL
*       - \ref CY_SMIF_NO_SFDP_SUPPORT
*       - \ref CY_SMIF_EXCEED_TIMEOUT
*
*******************************************************************************/
cy_en_smif_status_t Cy_SMIF_Memslot_SfdpDetect(SMIF_Type *base,
                                    cy_stc_smif_mem_device_cfg_t *device,
                                    cy_en_smif_slave_select_t slaveSelect,
                                    cy_en_smif_data_select_t dataSelect,
                                    cy_stc_smif_context_t *context)
{
    /* Check the input parameters */
    CY_ASSERT_L1(NULL != device);
    
    uint8_t sfdpBuffer[CY_SMIF_SFDP_LENGTH];
    uint8_t sfdpAddress[CY_SMIF_SFDP_ADDRESS_LENGTH] = {0x00U, 0x00U, 0x00U};
    cy_en_smif_status_t result = CY_SMIF_NO_SFDP_SUPPORT;
    cy_stc_smif_mem_cmd_t *cmdSfdp = device->readSfdpCmd;

    /* Initialize the SFDP buffer */
    for (uint32_t i = 0U; i < CY_SMIF_SFDP_LENGTH; i++)
    {
        sfdpBuffer[i] = 0U;
    }
    
    /* Slave slot initialization */
    Cy_SMIF_SetDataSelect(base, slaveSelect, dataSelect);

    if (NULL != cmdSfdp)
    {
        /* Get the SDFP header and all parameter headers content into sfdpBuffer[] */
        result = SfdpReadBuffer(base, 
                               cmdSfdp,
                               sfdpAddress,
                               slaveSelect,
                               HEADERS_LENGTH,
                               sfdpBuffer,
                               context);
    }
    
    /* Check if we support all parameter headers */
    if ((CY_SMIF_SUCCESS == result) && 
       (sfdpBuffer[PARAM_HEADERS_NUM] > PARAM_HEADER_NUM))
    {
        result = CY_SMIF_NO_SFDP_SUPPORT;
    }

    if (CY_SMIF_SUCCESS == result)
    {
        if((sfdpBuffer[CY_SMIF_SFDP_SING_BYTE_00] == (uint8_t)'S') &&
         (sfdpBuffer[CY_SMIF_SFDP_SING_BYTE_01] == (uint8_t)'F') &&
         (sfdpBuffer[CY_SMIF_SFDP_SING_BYTE_02] == (uint8_t)'D') &&
         (sfdpBuffer[CY_SMIF_SFDP_SING_BYTE_03] == (uint8_t)'P') &&
         (sfdpBuffer[CY_SMIF_SFDP_MINOR_REV] >= CY_SMIF_SFDP_JEDEC_REV_B) &&
         (sfdpBuffer[CY_SMIF_SFDP_MAJOR_REV] == CY_SMIF_SFDP_MAJOR_REV_1))
        {
            /* Find JEDEC SFDP Basic SPI Flash Parameter Header */
            uint32_t id = (BASIC_SPI_ID_MSB << BITS_IN_BYTE) |
                           BASIC_SPI_ID_LSB;
            uint32_t basicSpiTableLength;
            result = SfdpFindParameterTableAddress(id,
                                               sfdpBuffer,
                                               sfdpAddress,
                                               &basicSpiTableLength);
          
            if (CY_SMIF_SUCCESS == result)
            { 
                /* Get the JEDEC basic flash parameter table content into sfdpBuffer[] */
                result = SfdpReadBuffer(base, 
                                       cmdSfdp,
                                       sfdpAddress,
                                       slaveSelect,
                                       basicSpiTableLength,
                                       sfdpBuffer,
                                       context);

                /* The number of address bytes used by the memory slave device */
                device->numOfAddrBytes = SfdpGetNumOfAddrBytes(sfdpBuffer);

                /* The external memory size */
                device->memSize = SfdpGetMemoryDensity(sfdpBuffer);
             
                /* The page size */
                device->programSize = SfdpGetPageSize(sfdpBuffer);

                /* This specifies the Read command. The preference order Quad>Dual>SPI */
                cy_stc_smif_mem_cmd_t *cmdRead = device->readCmd;
                SfdpGetReadCmdParams(sfdpBuffer, dataSelect, cmdRead);

                /* The Write Enable command */
                SfdpSetWriteEnableCommand(device->writeEnCmd);               

                /* The Write Disable command */
                SfdpSetWriteDisableCommand(device->writeDisCmd); 
                
                /* The program command */
                SfdpSetProgramCommand(device->programCmd); 

                /* The busy mask for the status registers */
                device->stsRegBusyMask = CY_SMIF_STS_REG_BUSY_MASK;
                            
                /* The command to read the WIP-containing status register */
                SfdpSetWipStatusRegisterCommand(device->readStsRegWipCmd);

                /* The command to write into the QE-containing status register */
                SfdpGetQuadEnableParameters(device, sfdpBuffer);

                /* Chip Erase command */
                SfdpSetChipEraseCommand(device->chipEraseCmd);
                
                /* Find the sector Erase command type with 3-bytes addressing */
                uint32_t eraseTypeOffset;
                eraseTypeOffset = SfdpGetSectorEraseCommand(device->eraseCmd, sfdpBuffer);
                
                /* The Erase sector size */
                device->eraseSize = (0x01UL << (uint32_t)sfdpBuffer[eraseTypeOffset - 1UL]);

                /* Erase Time Type */
                device->eraseTime = SfdpGetEraseTime(eraseTypeOffset, sfdpBuffer);
                
                /* Chip Erase Time */
                device->chipEraseTime = SfdpGetChipEraseTime(sfdpBuffer);

                /* Page Program Time */
                device->programTime = SfdpGetPageProgramTime(sfdpBuffer);
            }
        }
        else
        {
            result = CY_SMIF_NO_SFDP_SUPPORT;
        }
    }

    return(result);
}

#if defined(__cplusplus)
}
#endif

#endif /* CY_IP_MXSMIF */

/* [] END OF FILE */
