/**
  ******************************************************************************
  * @file    stm32_user_driver.c
  * @author  MCD Application Team / User Driver
  * @brief   This file implements the USER driver for the ISSI IS25WX256
  *          (256 Mbit) Octal xSPI NOR Flash, using the STM32 HAL_XSPI driver,
  *          running the memory in Octal DDR (8D-8D-8D) protocol with DQS.
  *
  *          All opcodes, dummy-cycle counts, register maps and timings come
  *          from the ISSI "IS25LX256/128 IS25WX256/128" datasheet, Rev. A14
  *          (05/12/2026), referenced as [DS] in comments below, with section
  *          numbers (e.g. [DS 8.6]).
  *
  *          HAL_XSPI usage (XSPI_RegularCmdTypeDef field set, IOSelect,
  *          DQSMode, ...) matches the STM32N6xx HAL XSPI driver, which does
  *          NOT expose a SIOOMode field (unlike the U5/H5/H7RS XSPI_CCR_SIOO
  *          variants) -- this driver targets the N6 field layout only.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32_extmem.h"
#include "stm32_extmem_conf.h"
#if EXTMEM_DRIVER_USER == 1
#include "stm32_user_driver_api.h"
#include "stm32_user_driver_type.h"
#include "is25wx256_conf.h"
#include "is25wx256.h"
#include <string.h>

/** @defgroup USER USER driver
  * @ingroup EXTMEM_DRIVER
  * @{
  */

/* Private Macro ------------------------------------------------------------*/
/** @defgroup USER_Private_Macro Private Macro
  * @{
  */

/* Global timeout used for blocking HAL_XSPI_xxx calls (instruction phase,
   register read/write, autopolling setup, ...). Erase/Program use dedicated
   autopolling timeouts derived from the datasheet (is25wx256.h). */
#define IS25WX256_HAL_TIMEOUT_DEFAULT_MS      (100U)

/* Helper to bail out early on HAL error */
#define IS25WX256_CHECK_HAL(hal_call_)                                       \
  do {                                                                       \
    if ((hal_call_) != HAL_OK)                                               \
    {                                                                        \
      return EXTMEM_DRIVER_USER_ERROR_1;                                     \
    }                                                                        \
  } while (0)

/**
  * @}
  */
/* Private typedefs ---------------------------------------------------------*/

/* EXTMEM_USER_MemInfoTypeDef is provided by the project (declared in
   stm32_user_driver_type.h alongside EXTMEM_DRIVER_USER_ObjectTypeDef):
   typedef struct
   {
     uint8_t  MemSize;   Specifies the memory size as a power of 2
     uint32_t MpuCache;  MPU Cache
   } EXTMEM_USER_MemInfoTypeDef;
*/

/* Private variables ---------------------------------------------------------*/

/**
  * @brief Per-instance runtime context for the IS25WX256 driver.
  *        Allocated statically and linked through UserObject->PtrUserDriver.
  */
typedef struct
{
  XSPI_HandleTypeDef *hxspi;
  uint8_t             MemoryMappedModeEnabled;
} IS25WX256_Ctx_t;

static IS25WX256_Ctx_t IS25WX256_Ctx = {0};

/* Private functions ---------------------------------------------------------*/

static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_SendInstr(XSPI_HandleTypeDef *hxspi,
                                                              uint32_t Instruction,
                                                              uint32_t InstructionMode,
                                                              uint32_t Address,
                                                              uint32_t AddressMode,
                                                              uint32_t AddressSize,
                                                              uint32_t DataMode,
                                                              uint32_t DataLength,
                                                              uint32_t DummyCycles);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteEnable(XSPI_HandleTypeDef *hxspi);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_AutoPollingReady(XSPI_HandleTypeDef *hxspi, uint32_t TimeoutMs);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ReadStatusReg(XSPI_HandleTypeDef *hxspi, uint8_t *Status);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ReadFlagStatusReg(XSPI_HandleTypeDef *hxspi, uint8_t *FlagStatus);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteVolatileCfgReg(XSPI_HandleTypeDef *hxspi,
                                                                       uint8_t RegAddrLSB,
                                                                       uint8_t Value);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteNonVolatileCfgReg(XSPI_HandleTypeDef *hxspi,
                                                                          uint8_t RegAddrLSB,
                                                                          uint8_t Value);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_EnterOctalDdrMode(XSPI_HandleTypeDef *hxspi);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ResetMemory(XSPI_HandleTypeDef *hxspi);
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ConfigureXSPI(XSPI_HandleTypeDef *hxspi);

/* ============================================================================
 *                          LOW LEVEL HELPERS
 * ==========================================================================*/

/**
  * @brief  Generic helper building and sending an XSPI regular command in
  *         Octal DDR (8D-8D-8D) protocol, then optionally transmitting or
  *         receiving the data phase.
  * @note   Instruction is sent on 8 lines in DTR mode, matching the
  *         IS25WX256 Octal DDR command phase [DS 4.]. The STM32N6 HAL_XSPI
  *         driver has no SIOOMode field, so each HAL_XSPI_Command() call
  *         re-sends the full instruction phase, which matches the desired
  *         behavior here.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_SendInstr(XSPI_HandleTypeDef *hxspi,
                                                              uint32_t Instruction,
                                                              uint32_t InstructionMode,
                                                              uint32_t Address,
                                                              uint32_t AddressMode,
                                                              uint32_t AddressSize,
                                                              uint32_t DataMode,
                                                              uint32_t DataLength,
                                                              uint32_t DummyCycles)
{
  XSPI_RegularCmdTypeDef sCommand = {0};

  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;

  sCommand.Instruction           = Instruction;
  sCommand.InstructionMode       = InstructionMode;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = (InstructionMode == HAL_XSPI_INSTRUCTION_NONE) ?
                                    HAL_XSPI_INSTRUCTION_DTR_DISABLE : HAL_XSPI_INSTRUCTION_DTR_ENABLE;

  sCommand.Address               = Address;
  sCommand.AddressMode           = AddressMode;
  sCommand.AddressWidth          = AddressSize;
  sCommand.AddressDTRMode        = (AddressMode == HAL_XSPI_ADDRESS_NONE) ?
                                    HAL_XSPI_ADDRESS_DTR_DISABLE : HAL_XSPI_ADDRESS_DTR_ENABLE;

  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;

  sCommand.DataMode               = DataMode;
  sCommand.DataDTRMode            = (DataMode == HAL_XSPI_DATA_NONE) ?
                                     HAL_XSPI_DATA_DTR_DISABLE : HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DataLength              = DataLength;

  sCommand.DummyCycles             = DummyCycles;

  sCommand.DQSMode                 = (DataMode == HAL_XSPI_DATA_NONE) ?
                                      HAL_XSPI_DQS_DISABLE : HAL_XSPI_DQS_ENABLE;

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Sends the WRITE ENABLE command (06h) [DS 8.7].
  *         Must precede every PROGRAM, ERASE and register-WRITE command.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteEnable(XSPI_HandleTypeDef *hxspi)
{
  return IS25WX256_SendInstr(hxspi, IS25WX256_CMD_WRITE_ENABLE,
                              HAL_XSPI_INSTRUCTION_8_LINES, 0, HAL_XSPI_ADDRESS_NONE, 0,
                              HAL_XSPI_DATA_NONE, 0, 0);
}

/**
  * @brief  Reads the 8-bit Status Register (05h) [DS 8.8 / Table 8.7].
  *         In Octal DDR mode, 8 dummy cycles are required before data
  *         output [DS Table 8.1: READ STATUS REGISTER, Octal DDR dummy = 8].
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ReadStatusReg(XSPI_HandleTypeDef *hxspi, uint8_t *Status)
{
  XSPI_RegularCmdTypeDef sCommand = {0};

  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_READ_STATUS_REG;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.AddressMode           = HAL_XSPI_ADDRESS_NONE;
  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode               = HAL_XSPI_DATA_8_LINES;
  sCommand.DataDTRMode            = HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DataLength              = 2U; /* DDR transfers a minimum of 2 bytes; we keep only byte 0 */
  sCommand.DummyCycles             = 8U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_ENABLE;

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  uint8_t buf[2] = {0xFFu, 0xFFu};
  IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, buf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  *Status = buf[0];
  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Reads the 8-bit Flag Status Register (70h) [DS 8.8 / Table 6.4].
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ReadFlagStatusReg(XSPI_HandleTypeDef *hxspi, uint8_t *FlagStatus)
{
  XSPI_RegularCmdTypeDef sCommand = {0};

  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_READ_FLAG_STATUS_REG;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.AddressMode           = HAL_XSPI_ADDRESS_NONE;
  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode               = HAL_XSPI_DATA_8_LINES;
  sCommand.DataDTRMode            = HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DataLength              = 2U;
  sCommand.DummyCycles             = 8U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_ENABLE;

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  uint8_t buf[2] = {0xFFu, 0xFFu};
  IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, buf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  *FlagStatus = buf[0];
  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Polls the Status Register WIP bit until the memory reports it is
  *         no longer busy, or until TimeoutMs has elapsed [DS 6.1].
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_AutoPollingReady(XSPI_HandleTypeDef *hxspi, uint32_t TimeoutMs)
{
  uint32_t tickstart = HAL_GetTick();
  uint8_t  sr = 0;

  do
  {
    if (IS25WX256_ReadStatusReg(hxspi, &sr) != EXTMEM_DRIVER_USER_OK)
    {
      return EXTMEM_DRIVER_USER_ERROR_2;
    }

    if ((sr & IS25WX256_SR_WIP) == 0U)
    {
      return EXTMEM_DRIVER_USER_OK;
    }

    if ((HAL_GetTick() - tickstart) > TimeoutMs)
    {
      return EXTMEM_DRIVER_USER_ERROR_3;
    }
  } while (1);
}

/**
  * @brief  Writes a single byte into the Volatile Configuration Register at
  *         the given LSB address (81h) [DS 8.9 / Table 6.6]. Effective
  *         immediately, no autopolling delay required besides the HAL
  *         transfer itself.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteVolatileCfgReg(XSPI_HandleTypeDef *hxspi,
                                                                       uint8_t RegAddrLSB,
                                                                       uint8_t Value)
{
  EXTMEM_DRIVER_USER_StatusTypeDef status;

  status = IS25WX256_WriteEnable(hxspi);
  if (status != EXTMEM_DRIVER_USER_OK)
  {
    return status;
  }

  XSPI_RegularCmdTypeDef sCommand = {0};
  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_WRITE_V_CFG_REG;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.Address                = (uint32_t)RegAddrLSB; /* main array address scheme, only LSB used [DS 6.5] */
  sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
  sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS; /* Octal DDR is always 4-byte address [DS 4.] */
  sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
  sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
  sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DataLength              = 2U; /* DDR minimum transfer = 2 bytes; pad with same value */
  sCommand.DummyCycles             = 0U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_DISABLE; /* not used on writes [DS DQS pin description] */

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  uint8_t buf[2] = {Value, Value};
  IS25WX256_CHECK_HAL(HAL_XSPI_Transmit(hxspi, buf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Writes a single byte into the Nonvolatile Configuration Register
  *         at the given LSB address (B1h) [DS 8.9 / Table 6.5]. Self-timed,
  *         duration tWNVCR (datasheet typ 0.2s, max 1s).
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_WriteNonVolatileCfgReg(XSPI_HandleTypeDef *hxspi,
                                                                          uint8_t RegAddrLSB,
                                                                          uint8_t Value)
{
  EXTMEM_DRIVER_USER_StatusTypeDef status;

  status = IS25WX256_WriteEnable(hxspi);
  if (status != EXTMEM_DRIVER_USER_OK)
  {
    return status;
  }

  XSPI_RegularCmdTypeDef sCommand = {0};
  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_WRITE_NV_CFG_REG;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.Address                = (uint32_t)RegAddrLSB;
  sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
  sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
  sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
  sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
  sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DataLength              = 2U;
  sCommand.DummyCycles             = 0U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_DISABLE;

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  uint8_t buf[2] = {Value, Value};
  IS25WX256_CHECK_HAL(HAL_XSPI_Transmit(hxspi, buf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  /* Self-timed operation: poll WIP [DS 8.9] */
  return IS25WX256_AutoPollingReady(hxspi, IS25WX256_TIMEOUT_WRNVCR_MS);
}

/**
  * @brief  Issues RESET ENABLE (66h) followed by RESET MEMORY (99h) with the
  *         mandatory tSHSL2 de-selection gap between the two, as required by
  *         [DS 8.3]. The sequence is issued both in legacy 1-line SPI
  *         framing (works when the device boots/lives in Extended SPI) and
  *         in Octal DDR framing (works when the device is already running
  *         in Octal DDR mode from a previous Init()), so this function is
  *         safe to call regardless of the memory's current protocol state.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ResetMemory(XSPI_HandleTypeDef *hxspi)
{
  XSPI_RegularCmdTypeDef sCommand = {0};

  /* --- RESET ENABLE (66h), 1S-0-0 --- */
  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_RESET_ENABLE;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_1_LINE;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  sCommand.AddressMode           = HAL_XSPI_ADDRESS_NONE;
  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode                = HAL_XSPI_DATA_NONE;
  sCommand.DummyCycles             = 0U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_DISABLE;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  /* tSHSL2 minimum de-selection time between the two commands [DS Fig 8.1] */
  HAL_Delay(1);

  /* --- RESET MEMORY (99h), 1S-0-0 --- */
  sCommand.Instruction = IS25WX256_CMD_RESET_MEMORY;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  HAL_Delay(1);

  /* Also try the Octal-DDR framed variant, in case the device was already
     running in Octal DDR mode when this function is called (e.g. DeInit
     called after a previous successful Init): the 1S-0-0 RESET ENABLE/
     RESET MEMORY above is decoded on DQ0 only and works from any protocol
     per [DS 8.3], so this extra 8D-0-0 attempt is not mandatory, but is
     kept here defensively and is harmless if ignored by the memory. */
  sCommand.Instruction        = IS25WX256_CMD_RESET_ENABLE;
  sCommand.InstructionMode    = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  (void)HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS);
  HAL_Delay(1);
  sCommand.Instruction        = IS25WX256_CMD_RESET_MEMORY;
  (void)HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS);

  /* Reset recovery time: device deselected & standby -> max 5us per
     [DS Table 9.5, tRHSL1]; use a generous 1ms software margin. */
  HAL_Delay(1);

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Switches the memory from its default Extended SPI protocol to
  *         Octal DDR (8D-8D-8D) with DQS, and configures it for 4-byte
  *         addressing, by writing the relevant Nonvolatile Configuration
  *         Register bytes [DS Table 6.5]:
  *           - address 05h = FEh  -> 4-byte address
  *           - address 00h = E7h  -> Octal DDR with DQS enabled
  *         The WRITE NONVOLATILE CONFIGURATION REGISTER command (B1h) is
  *         sent in Extended SPI (1S-1S-1S) framing since the device is still
  *         in that protocol at this point of the sequence.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_EnterOctalDdrMode(XSPI_HandleTypeDef *hxspi)
{
  XSPI_RegularCmdTypeDef sCommand = {0};
  EXTMEM_DRIVER_USER_StatusTypeDef status;

  /* --- Step 1: WRITE ENABLE (06h), 1S-0-0 --- */
  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_WRITE_ENABLE;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_1_LINE;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
  sCommand.AddressMode           = HAL_XSPI_ADDRESS_NONE;
  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode                = HAL_XSPI_DATA_NONE;
  sCommand.DummyCycles             = 0U;
  sCommand.DQSMode                 = HAL_XSPI_DQS_DISABLE;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  /* --- Step 2: WRITE NONVOLATILE CONFIGURATION REGISTER (B1h), address
     05h, value FEh = 4-byte address mode [DS Table 6.5], 1S-1S-1S --- */
  sCommand.Instruction       = IS25WX256_CMD_WRITE_NV_CFG_REG;
  sCommand.InstructionMode   = HAL_XSPI_INSTRUCTION_1_LINE;
  sCommand.Address           = IS25WX256_CR_ADDR_4BYTE_ADDR_CFG;
  sCommand.AddressMode       = HAL_XSPI_ADDRESS_1_LINE;
  sCommand.AddressWidth      = HAL_XSPI_ADDRESS_24_BITS; /* still in 3-byte addressing by default */
  sCommand.AddressDTRMode    = HAL_XSPI_ADDRESS_DTR_DISABLE;
  sCommand.DataMode          = HAL_XSPI_DATA_1_LINE;
  sCommand.DataDTRMode       = HAL_XSPI_DATA_DTR_DISABLE;
  sCommand.DataLength        = 1U;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
  {
    uint8_t val = IS25WX256_ADDR_MODE_4BYTE;
    IS25WX256_CHECK_HAL(HAL_XSPI_Transmit(hxspi, &val, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
  }

  /* Self-timed: poll WIP through legacy SPI status read (05h, 1S-0-1) */
  {
    uint32_t tickstart = HAL_GetTick();
    uint8_t  sr;
    XSPI_RegularCmdTypeDef sPoll = {0};
    sPoll.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sPoll.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sPoll.Instruction           = IS25WX256_CMD_READ_STATUS_REG;
    sPoll.InstructionMode       = HAL_XSPI_INSTRUCTION_1_LINE;
    sPoll.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sPoll.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sPoll.AddressMode           = HAL_XSPI_ADDRESS_NONE;
    sPoll.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
    sPoll.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sPoll.DataMode               = HAL_XSPI_DATA_1_LINE;
    sPoll.DataDTRMode            = HAL_XSPI_DATA_DTR_DISABLE;
    sPoll.DataLength             = 1U;
    sPoll.DummyCycles            = 0U;
    sPoll.DQSMode                = HAL_XSPI_DQS_DISABLE;

    do
    {
      IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sPoll, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
      IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, &sr, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
      if ((sr & IS25WX256_SR_WIP) == 0U)
      {
        break;
      }
      if ((HAL_GetTick() - tickstart) > IS25WX256_TIMEOUT_WRNVCR_MS)
      {
        return EXTMEM_DRIVER_USER_ERROR_4;
      }
    } while (1);
  }

  /* --- Step 3: WRITE ENABLE again (required before next NVCR write) --- */
  sCommand.Instruction = IS25WX256_CMD_WRITE_ENABLE;
  sCommand.AddressMode = HAL_XSPI_ADDRESS_NONE;
  sCommand.DataMode    = HAL_XSPI_DATA_NONE;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  /* --- Step 4: WRITE NONVOLATILE CONFIGURATION REGISTER (B1h), address
     00h, value E7h = Octal DDR with DQS [DS Table 6.5], using NOW a 4-byte
     address phase since the device just switched address mode --- */
  sCommand.Instruction    = IS25WX256_CMD_WRITE_NV_CFG_REG;
  sCommand.Address        = IS25WX256_CR_ADDR_IO_MODE;
  sCommand.AddressMode    = HAL_XSPI_ADDRESS_1_LINE;
  sCommand.AddressWidth   = HAL_XSPI_ADDRESS_32_BITS; /* now in 4-byte address mode */
  sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
  sCommand.DataMode       = HAL_XSPI_DATA_1_LINE;
  sCommand.DataDTRMode    = HAL_XSPI_DATA_DTR_DISABLE;
  sCommand.DataLength     = 1U;
  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
  {
    uint8_t val = IS25WX256_IOMODE_OCTAL_DDR;
    IS25WX256_CHECK_HAL(HAL_XSPI_Transmit(hxspi, &val, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
  }

  /* Poll WIP one last time in (still) Extended SPI 1-line framing: this
     WRITE NONVOLATILE CONFIGURATION REGISTER command only takes effect
     after S# is driven HIGH and the internal write completes, and the
     protocol bit just written does not change the framing of the *current*
     command, so the chip is still answering 1S-0-1 READ STATUS REGISTER at
     this point. */
  {
    uint32_t tickstart = HAL_GetTick();
    uint8_t  sr;
    XSPI_RegularCmdTypeDef sPoll = {0};
    sPoll.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sPoll.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sPoll.Instruction           = IS25WX256_CMD_READ_STATUS_REG;
    sPoll.InstructionMode       = HAL_XSPI_INSTRUCTION_1_LINE;
    sPoll.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sPoll.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sPoll.AddressMode           = HAL_XSPI_ADDRESS_NONE;
    sPoll.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
    sPoll.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sPoll.DataMode               = HAL_XSPI_DATA_1_LINE;
    sPoll.DataDTRMode            = HAL_XSPI_DATA_DTR_DISABLE;
    sPoll.DataLength             = 1U;
    sPoll.DummyCycles            = 0U;
    sPoll.DQSMode                = HAL_XSPI_DQS_DISABLE;

    do
    {
      IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sPoll, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
      IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, &sr, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
      if ((sr & IS25WX256_SR_WIP) == 0U)
      {
        break;
      }
      if ((HAL_GetTick() - tickstart) > IS25WX256_TIMEOUT_WRNVCR_MS)
      {
        return EXTMEM_DRIVER_USER_ERROR_5;
      }
    } while (1);
  }

  /* The device only switches to Octal DDR protocol after the NEXT power
     cycle or software/hardware RESET [DS 6.3, 6.4: "Nonvolatile to
     internal register download after power-on or reset"]. Issue a
     software reset now so the new NVCR settings (4-byte address + Octal
     DDR/DQS) become active in the internal configuration register. */
  status = IS25WX256_ResetMemory(hxspi);
  if (status != EXTMEM_DRIVER_USER_OK)
  {
    return status;
  }

  /* From this point on the memory answers in Octal DDR (8D-8D-8D), 4-byte
     address, DQS enabled -- all subsequent commands in this driver use
     that framing. */
  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief  Configures the XSPI peripheral instance (clock prescaler, memory
  *         size, chip-select high time, free-running clock, sample
  *         shifting, ...) for operation with the IS25WX256 in Octal DDR
  *         mode. Frequency target left conservative (<=133MHz, see
  *         [DS Table 9.3], to remain compatible with both ECC ON and OFF
  *         settings, and to leave headroom on PCB layouts not specifically
  *         optimized for 200MHz DDR); raise ClockPrescaler once your signal
  *         integrity / dummy-cycle tuning is validated up to 200MHz.
  * @note   hxspi->Init.ClockPrescaler is expected to already be configured
  *         by the application / MX_XSPIx_Init() prior to calling
  *         EXTMEM_DRIVER_USER_Init(); only flash-specific fields are forced
  *         here. DelayHoldQuarterCycle is intentionally NOT set: it is
  *         deprecated on STM32N6xx products and unused by this HAL version.
  */
static EXTMEM_DRIVER_USER_StatusTypeDef IS25WX256_ConfigureXSPI(XSPI_HandleTypeDef *hxspi)
{
  /* Memory size expressed as log2(bytes) per HAL_XSPI convention:
     256 Mbit = 32 MByte = 2^25 bytes -> MemorySize = HAL_XSPI_SIZE_32MB (25) */
  hxspi->Init.MemorySize               = HAL_XSPI_SIZE_32MB;
  hxspi->Init.ChipSelectHighTimeCycle  = 2U;
  hxspi->Init.FreeRunningClock         = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi->Init.ClockMode                = HAL_XSPI_CLOCK_MODE_0;
  hxspi->Init.WrapSize                 = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi->Init.SampleShifting           = HAL_XSPI_SAMPLE_SHIFT_NONE; /* DQS handles read data alignment in Octal DDR */
  hxspi->Init.ChipSelectBoundary       = 0U;
  hxspi->Init.MemoryType               = HAL_XSPI_MEMTYPE_MACRONIX; /* generic octal NOR memory model, no vendor-specific quirks needed */

  IS25WX256_CHECK_HAL(HAL_XSPI_Init(hxspi));

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @}
  */

/** @defgroup USER_Exported_Functions Exported Functions
  * @{
  */

/**
  * @brief Initializes the USER driver.
  * @param MemoryId Memory ID.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Sequence [DS 8.19 Power-up, 8.3 Software reset, 6.4 NVCR, 4. xSPI
  * protocol]:
  *   1. Wait tVSL after power-up (handled by board/clock startup, assumed
  *      already elapsed when Init() is called).
  *   2. Configure the XSPI peripheral.
  *   3. Software RESET (in case of warm boot / debugger reset) so the part
  *      starts from a known state regardless of its previous configuration.
  *   4. Switch the memory to Octal DDR (8D-8D-8D) + DQS + 4-byte address via
  *      the Nonvolatile Configuration Register, then RESET again so the new
  *      configuration is loaded into the internal configuration register.
  *   5. Verify communication by reading back the JEDEC ID (Table 7.1).
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Init(uint32_t MemoryId,
                                                                EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_NOTSUPPORTED;

  if (UserObject == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  UserObject->MemID = MemoryId;     /* Store the memory ID; can be used to control multiple user memories. */
  UserObject->PtrUserDriver = NULL; /* Can be used to link data with the memory ID. */

  memset(&IS25WX256_Ctx, 0, sizeof(IS25WX256_Ctx));
  IS25WX256_Ctx.hxspi = IS25WX256_XSPI_HANDLE;

  /* 1-2: Configure XSPI peripheral */
  retr = IS25WX256_ConfigureXSPI(IS25WX256_Ctx.hxspi);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  /* 3: Bring the memory to a known state (Extended SPI 1S-1S-1S, 3-byte
     address, default NVCR settings) before reconfiguring it. */
  retr = IS25WX256_ResetMemory(IS25WX256_Ctx.hxspi);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  /* 4: Switch to Octal DDR (8D-8D-8D) with DQS and 4-byte addressing */
  retr = IS25WX256_EnterOctalDdrMode(IS25WX256_Ctx.hxspi);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  /* 5: Sanity check -- read JEDEC ID in Octal DDR and verify the
     manufacturer/memory-type bytes match the expected device
     [DS Table 7.1, 8.4 READ ID Operation]. */
  {
    uint8_t id[3] = {0};
    XSPI_RegularCmdTypeDef sCommand = {0};

    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = IS25WX256_CMD_READ_ID;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address               = 0U;
    sCommand.AddressMode           = HAL_XSPI_ADDRESS_NONE; /* [DS Table 8.1] READ ID: Address Bytes = 0 */
    sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode               = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode            = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength              = 4U; /* even count for DDR; we use bytes 0..2 (MID/DID/DID) */
    sCommand.DummyCycles             = 8U; /* [DS Table 8.1] READ ID, Octal DDR dummy = 8 */
    sCommand.DQSMode                 = HAL_XSPI_DQS_ENABLE;

    if (HAL_XSPI_Command(IS25WX256_Ctx.hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS) != HAL_OK)
    {
      return EXTMEM_DRIVER_USER_ERROR_6;
    }

    uint8_t buf[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    if (HAL_XSPI_Receive(IS25WX256_Ctx.hxspi, buf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS) != HAL_OK)
    {
      return EXTMEM_DRIVER_USER_ERROR_6;
    }
    id[0] = buf[0]; /* Manufacturer ID */
    id[1] = buf[1]; /* Memory Type    */
    id[2] = buf[2]; /* Memory Density */

    if (id[0] != IS25WX256_MANUFACTURER_ID || id[1] != IS25WX256_MEMORY_TYPE_1V8)
    {
      return EXTMEM_DRIVER_USER_ERROR_7;
    }
  }

  UserObject->PtrUserDriver = (void *)&IS25WX256_Ctx;

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Deinitializes the USER driver.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_DeInit(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_NOTSUPPORTED;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;

  if (ctx->MemoryMappedModeEnabled != 0U)
  {
    if (HAL_XSPI_Abort(ctx->hxspi) != HAL_OK)
    {
      return EXTMEM_DRIVER_USER_ERROR_1;
    }
    ctx->MemoryMappedModeEnabled = 0U;
  }

  /* Bring the memory back to its default power-on state (Extended SPI,
     3-byte address) so a subsequent Init() (or another driver/bootloader)
     finds it in a known configuration [DS 8.3]. */
  retr = IS25WX256_ResetMemory(ctx->hxspi);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  if (HAL_XSPI_DeInit(ctx->hxspi) != HAL_OK)
  {
    return EXTMEM_DRIVER_USER_ERROR_2;
  }

  UserObject->PtrUserDriver = NULL;

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Reads data from the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Data Pointer to the data buffer to store the read data.
  * @param Size Size of data to read (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Uses the 4-BYTE DDR OCTAL I/O FAST READ command (FDh), the only Octal
  * DDR read opcode listed for this protocol [DS Table 8.1: "DDR OCTAL I/O
  * FAST READ", 1S-8D-8D in Extended SPI column / 8D-8D-8D in Octal DDR
  * column, fixed 4-byte address, dummy = 16 in Octal DDR mode].
  * Per [DS 8.6 / GENERAL DESCRIPTION], DDR transfers move a minimum of 2
  * bytes and the LSB of the starting address must be 0; this function
  * transparently handles odd start address / odd size by reading into a
  * small aligned scratch buffer at the edges.
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Read(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                uint32_t Address, uint8_t *Data, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_NOTSUPPORTED;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL || Data == NULL || Size == 0U)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;
  XSPI_HandleTypeDef *hxspi = ctx->hxspi;

  uint32_t addr = Address;
  uint32_t size = Size;
  uint8_t *dst  = Data;
  uint8_t  edgeBuf[2];

  /* Datasheet constraint: "minimum transferred data size is 2-bytes in DDR
     mode, so the LSB of starting address must be always 0" [DS 8.6]. */
  if ((addr & 0x1U) != 0U)
  {
    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = IS25WX256_CMD_DDR_OCTAL_IO_FAST_READ;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address                = (addr & ~0x1U);
    sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength               = 2U;
    sCommand.DummyCycles              = IS25WX256_FAST_READ_DUMMY_CYCLES;
    sCommand.DQSMode                  = HAL_XSPI_DQS_ENABLE;

    IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
    IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, edgeBuf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

    *dst = edgeBuf[1]; /* odd-address byte is the 2nd byte of the aligned word */
    dst++;
    addr++;
    size--;
    if (size == 0U)
    {
      return EXTMEM_DRIVER_USER_OK;
    }
  }

  uint32_t mainSize = size & ~0x1U; /* even part, bulk transfer */
  uint32_t tailSize = size & 0x1U;  /* 0 or 1 trailing byte */

  if (mainSize > 0U)
  {
    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = IS25WX256_CMD_DDR_OCTAL_IO_FAST_READ;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address                = addr;
    sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength               = mainSize;
    sCommand.DummyCycles              = IS25WX256_FAST_READ_DUMMY_CYCLES;
    sCommand.DQSMode                  = HAL_XSPI_DQS_ENABLE;

    IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
    IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, dst, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

    dst  += mainSize;
    addr += mainSize;
  }

  if (tailSize == 1U)
  {
    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = IS25WX256_CMD_DDR_OCTAL_IO_FAST_READ;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address                = addr;
    sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength               = 2U;
    sCommand.DummyCycles              = IS25WX256_FAST_READ_DUMMY_CYCLES;
    sCommand.DQSMode                  = HAL_XSPI_DQS_ENABLE;

    IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
    IS25WX256_CHECK_HAL(HAL_XSPI_Receive(hxspi, edgeBuf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
    *dst = edgeBuf[0];
  }

  retr = EXTMEM_DRIVER_USER_OK;
  return retr;
}

/**
  * @brief Writes data to the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Data Pointer to the data buffer to be written.
  * @param Size Size of data to be written (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Splits the transfer along 256-byte page boundaries [DS 8.11 PAGE
  * PROGRAM: "Program 1 to 256byte per Page"], issuing a WRITE ENABLE +
  * 4-BYTE EXTENDED OCTAL INPUT FAST PROGRAM (8Eh) for each page, then
  * polling WIP until completion (tPP, [DS Table 9.3]).
  * Honors the DDR even-byte-count / even-start-address constraint
  * described in [DS 8.11]: odd start/end addresses are padded with FFh as
  * recommended by the datasheet ("the input data shall start with FFh" /
  * "provide an extra data with FFh in the last falling edge of clock").
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Write(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                 uint32_t Address, const uint8_t *Data, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL || Data == NULL || Size == 0U)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;
  XSPI_HandleTypeDef *hxspi = ctx->hxspi;

  uint32_t remaining = Size;
  uint32_t addr       = Address;
  const uint8_t *src  = Data;

  while (remaining > 0U)
  {
    uint32_t pageOffset = addr % IS25WX256_PAGE_SIZE;
    uint32_t chunk       = IS25WX256_PAGE_SIZE - pageOffset;
    if (chunk > remaining)
    {
      chunk = remaining;
    }

    /* Build a local, even-length, FFh-padded buffer for this chunk to
       respect the DDR 2-byte transfer granularity [DS 8.11]. */
    uint8_t  localBuf[IS25WX256_PAGE_SIZE + 2U];
    uint32_t txAddr = addr;
    uint32_t bufIdx = 0U;

    if ((txAddr & 0x1U) != 0U)
    {
      /* Odd starting address: keep even address, prepend FFh */
      localBuf[bufIdx++] = 0xFFu;
      txAddr--;
    }
    memcpy(&localBuf[bufIdx], src, chunk);
    bufIdx += chunk;

    if ((bufIdx & 0x1U) != 0U)
    {
      /* Odd total length: append FFh as last falling-edge data byte */
      localBuf[bufIdx++] = 0xFFu;
    }

    retr = IS25WX256_WriteEnable(hxspi);
    if (retr != EXTMEM_DRIVER_USER_OK)
    {
      return retr;
    }

    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = IS25WX256_CMD_4B_EXT_OCTAL_IN_FAST_PROG;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address                = txAddr;
    sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
    sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
    sCommand.DataLength               = bufIdx;
    sCommand.DummyCycles              = 0U;
    sCommand.DQSMode                  = HAL_XSPI_DQS_DISABLE; /* DQS not used for WRITE [DS DQS pin desc.] */

    IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));
    IS25WX256_CHECK_HAL(HAL_XSPI_Transmit(hxspi, localBuf, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

    retr = IS25WX256_AutoPollingReady(hxspi, IS25WX256_TIMEOUT_PAGE_PROGRAM_MS + 5U);
    if (retr != EXTMEM_DRIVER_USER_OK)
    {
      return retr;
    }

    /* Check for program/protection error via Flag Status Register
       [DS Table 6.4, bits 4 (program error) and 1 (protection error)]. */
    {
      uint8_t fsr = 0;
      retr = IS25WX256_ReadFlagStatusReg(hxspi, &fsr);
      if (retr != EXTMEM_DRIVER_USER_OK)
      {
        return retr;
      }
      if ((fsr & (IS25WX256_FSR_PROGRAM_ERROR | IS25WX256_FSR_PROTECTION)) != 0U)
      {
        (void)IS25WX256_SendInstr(hxspi, IS25WX256_CMD_CLEAR_FLAG_STATUS_REG, HAL_XSPI_INSTRUCTION_8_LINES,
                                   0, HAL_XSPI_ADDRESS_NONE, 0, HAL_XSPI_DATA_NONE, 0, 0);
        return EXTMEM_DRIVER_USER_ERROR_8;
      }
    }

    src       += chunk;
    addr      += chunk;
    remaining -= chunk;
  }

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Erases sectors in the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @param Address Memory address.
  * @param Size Size of data to erase (in bytes).
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Picks the most efficient erase granularity covering [Address,
  * Address+Size) among the 4KB subsector, 32KB subsector and 128KB sector
  * commands [DS 8.12, Table 8.1]:
  *   - 4-BYTE 4KB  SUBSECTOR ERASE  (21h) when Address/Size are 4KB aligned
  *     and Size < 32KB,
  *   - 4-BYTE 32KB SUBSECTOR ERASE  (5Ch) when 32KB aligned and Size < 128KB,
  *   - 4-BYTE 128KB SECTOR ERASE    (DCh) otherwise (128KB aligned chunks).
  * Any address within the target subsector/sector is a valid entry address
  * [DS Table 8.12], so this function always aligns down to the erase unit
  * boundary actually used and erases forward until full coverage of the
  * requested range; addresses/size not aligned to 4KB are rejected since
  * the device has no finer erase granularity.
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_EraseSector(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                       uint32_t Address, uint32_t Size)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL || Size == 0U)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  if ((Address % IS25WX256_SUBSECTOR_4K_SIZE) != 0U)
  {
    return EXTMEM_DRIVER_USER_ERROR_2; /* not aligned to the finest erase granularity */
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;
  XSPI_HandleTypeDef *hxspi = ctx->hxspi;

  uint32_t addr      = Address;
  uint32_t remaining = Size;

  while (remaining > 0U)
  {
    uint8_t  cmd;
    uint32_t unitSize;
    uint32_t timeoutMs;

    if (((addr % IS25WX256_SECTOR_SIZE) == 0U) && (remaining >= IS25WX256_SECTOR_SIZE))
    {
      cmd       = IS25WX256_CMD_4B_SECTOR_ERASE_128K;
      unitSize  = IS25WX256_SECTOR_SIZE;
      timeoutMs = IS25WX256_TIMEOUT_SECTOR_128K_MS + 50U;
    }
    else if (((addr % IS25WX256_SUBSECTOR_32K_SIZE) == 0U) && (remaining >= IS25WX256_SUBSECTOR_32K_SIZE))
    {
      cmd       = IS25WX256_CMD_4B_SUBSECTOR_ERASE_32K;
      unitSize  = IS25WX256_SUBSECTOR_32K_SIZE;
      timeoutMs = IS25WX256_TIMEOUT_SUBSECTOR_32K_MS + 50U;
    }
    else
    {
      cmd       = IS25WX256_CMD_4B_SUBSECTOR_ERASE_4K;
      unitSize  = IS25WX256_SUBSECTOR_4K_SIZE;
      timeoutMs = IS25WX256_TIMEOUT_SUBSECTOR_4K_MS + 50U;
    }

    retr = IS25WX256_WriteEnable(hxspi);
    if (retr != EXTMEM_DRIVER_USER_OK)
    {
      return retr;
    }

    /* ERASE Operations, 4-byte address, Octal DDR: 8-8-0, dummy=0
       [DS Table 8.1]. */
    XSPI_RegularCmdTypeDef sCommand = {0};
    sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
    sCommand.Instruction           = cmd;
    sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
    sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
    sCommand.Address                = addr;
    sCommand.AddressMode            = HAL_XSPI_ADDRESS_8_LINES;
    sCommand.AddressWidth           = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode         = HAL_XSPI_ADDRESS_DTR_ENABLE;
    sCommand.AlternateBytesMode     = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.AlternateBytesDTRMode  = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
    sCommand.DataMode                = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles             = 0U;
    sCommand.DQSMode                 = HAL_XSPI_DQS_DISABLE;

    IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

    retr = IS25WX256_AutoPollingReady(hxspi, timeoutMs);
    if (retr != EXTMEM_DRIVER_USER_OK)
    {
      return retr;
    }

    /* Check ERASE error / protection error [DS Table 6.4, bits 5 and 1] */
    {
      uint8_t fsr = 0;
      retr = IS25WX256_ReadFlagStatusReg(hxspi, &fsr);
      if (retr != EXTMEM_DRIVER_USER_OK)
      {
        return retr;
      }
      if ((fsr & (IS25WX256_FSR_ERASE_ERROR | IS25WX256_FSR_PROTECTION)) != 0U)
      {
        (void)IS25WX256_SendInstr(hxspi, IS25WX256_CMD_CLEAR_FLAG_STATUS_REG, HAL_XSPI_INSTRUCTION_8_LINES,
                                   0, HAL_XSPI_ADDRESS_NONE, 0, HAL_XSPI_DATA_NONE, 0, 0);
        return EXTMEM_DRIVER_USER_ERROR_3;
      }
    }

    if (unitSize >= remaining)
    {
      remaining = 0U;
    }
    else
    {
      remaining -= unitSize;
    }
    addr += unitSize;
  }

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Performs a mass erase of the USER memory.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Issues CHIP ERASE (60h), Octal DDR 8-0-0 [DS Table 8.1, 8.12]. The
  * command is rejected by the memory itself (Flag Status Register bits 1
  * and 5 set, write-enable-latch behaviour per [DS 8.12]) if any sector is
  * BP-protected; this is reported back as an error after polling WIP.
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_MassErase(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;
  XSPI_HandleTypeDef *hxspi = ctx->hxspi;

  retr = IS25WX256_WriteEnable(hxspi);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  retr = IS25WX256_SendInstr(hxspi, IS25WX256_CMD_CHIP_ERASE, HAL_XSPI_INSTRUCTION_8_LINES,
                              0, HAL_XSPI_ADDRESS_NONE, 0, HAL_XSPI_DATA_NONE, 0, 0);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  retr = IS25WX256_AutoPollingReady(hxspi, IS25WX256_TIMEOUT_CHIP_ERASE_MS);
  if (retr != EXTMEM_DRIVER_USER_OK)
  {
    return retr;
  }

  {
    uint8_t fsr = 0;
    retr = IS25WX256_ReadFlagStatusReg(hxspi, &fsr);
    if (retr != EXTMEM_DRIVER_USER_OK)
    {
      return retr;
    }
    if ((fsr & (IS25WX256_FSR_ERASE_ERROR | IS25WX256_FSR_PROTECTION)) != 0U)
    {
      (void)IS25WX256_SendInstr(hxspi, IS25WX256_CMD_CLEAR_FLAG_STATUS_REG, HAL_XSPI_INSTRUCTION_8_LINES,
                                 0, HAL_XSPI_ADDRESS_NONE, 0, HAL_XSPI_DATA_NONE, 0, 0);
      return EXTMEM_DRIVER_USER_ERROR_2;
    }
  }

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Enables memory-mapped mode for the USER device.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * Configures HAL_XSPI memory-mapped mode using the 4-BYTE DDR OCTAL I/O
  * FAST READ command (FDh) [DS Table 8.1] for reads. Writes are NOT
  * supported through the memory-mapped window for this device family (no
  * separate write opcode is set), in line with typical XIP/read-only usage
  * [DS 8.18 XIP MODE].
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Enable_MemoryMappedMode(EXTMEM_DRIVER_USER_ObjectTypeDef
    *UserObject)
{
  EXTMEM_DRIVER_USER_StatusTypeDef retr = EXTMEM_DRIVER_USER_NOTSUPPORTED;

  if (UserObject == NULL || UserObject->PtrUserDriver == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;
  XSPI_HandleTypeDef *hxspi = ctx->hxspi;

  XSPI_RegularCmdTypeDef sCommand = {0};
  sCommand.OperationType         = HAL_XSPI_OPTYPE_COMMON_CFG;
  sCommand.IOSelect              = HAL_XSPI_SELECT_IO_7_0;
  sCommand.Instruction           = IS25WX256_CMD_DDR_OCTAL_IO_FAST_READ;
  sCommand.InstructionMode       = HAL_XSPI_INSTRUCTION_8_LINES;
  sCommand.InstructionWidth      = HAL_XSPI_INSTRUCTION_8_BITS;
  sCommand.InstructionDTRMode    = HAL_XSPI_INSTRUCTION_DTR_ENABLE;
  sCommand.AddressMode           = HAL_XSPI_ADDRESS_8_LINES;
  sCommand.AddressWidth          = HAL_XSPI_ADDRESS_32_BITS;
  sCommand.AddressDTRMode        = HAL_XSPI_ADDRESS_DTR_ENABLE;
  sCommand.AlternateBytesMode    = HAL_XSPI_ALT_BYTES_NONE;
  sCommand.AlternateBytesDTRMode = HAL_XSPI_ALT_BYTES_DTR_DISABLE;
  sCommand.DataMode                = HAL_XSPI_DATA_8_LINES;
  sCommand.DataDTRMode             = HAL_XSPI_DATA_DTR_ENABLE;
  sCommand.DummyCycles              = IS25WX256_FAST_READ_DUMMY_CYCLES;
  sCommand.DQSMode                  = HAL_XSPI_DQS_ENABLE;

  IS25WX256_CHECK_HAL(HAL_XSPI_Command(hxspi, &sCommand, IS25WX256_HAL_TIMEOUT_DEFAULT_MS));

  XSPI_MemoryMappedTypeDef sMemMappedCfg = {0};
  sMemMappedCfg.TimeOutActivation  = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;
  sMemMappedCfg.TimeoutPeriodClock = 0U;

  IS25WX256_CHECK_HAL(HAL_XSPI_MemoryMapped(hxspi, &sMemMappedCfg));

  ctx->MemoryMappedModeEnabled = 1U;

  retr = EXTMEM_DRIVER_USER_OK;
  return retr;
}

/**
  * @brief Disables memory-mapped mode for the USER device.
  * @param UserObject Pointer to the USER driver object.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_Disable_MemoryMappedMode(
  EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject)
{
  if (UserObject == NULL || UserObject->PtrUserDriver == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  IS25WX256_Ctx_t *ctx = (IS25WX256_Ctx_t *)UserObject->PtrUserDriver;

  if (ctx->MemoryMappedModeEnabled == 0U)
  {
    return EXTMEM_DRIVER_USER_OK; /* nothing to do */
  }

  IS25WX256_CHECK_HAL(HAL_XSPI_Abort(ctx->hxspi));

  ctx->MemoryMappedModeEnabled = 0U;

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Retrieves the mapped address.
  * @param UserObject Pointer to the USER driver object.
  * @param BaseAddress Pointer to store the mapped base address.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_GetMapAddress(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                         uint32_t *BaseAddress)
{
  if (UserObject == NULL || UserObject->PtrUserDriver == NULL || BaseAddress == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  *BaseAddress = IS25WX256_XSPI_MM_BASE_ADDRESS;

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @brief Retrieves USER memory information.
  * @param UserObject Pointer to the USER driver object.
  * @param MemInfo Pointer to the USER memory information structure to be filled.
  * @retval @ref EXTMEM_DRIVER_USER_StatusTypeDef
  *
  * MemSize: total flash size expressed as a power of 2. IS25WX256 is
  * 256Mbit = 32MByte = 2^25 bytes [DS Features: "IS25WX256: 256Mbit/32Mbyte"].
  * MpuCache: MPU attribute to apply to the memory-mapped XSPI region; this
  * is a system/MPU-policy choice rather than a flash characteristic, so it
  * is taken from the project configuration (stm32_user_driver_conf.h).
  */
__weak EXTMEM_DRIVER_USER_StatusTypeDef EXTMEM_DRIVER_USER_GetInfo(EXTMEM_DRIVER_USER_ObjectTypeDef *UserObject,
                                                                   EXTMEM_USER_MemInfoTypeDef *MemInfo)
{
  if (UserObject == NULL || MemInfo == NULL)
  {
    return EXTMEM_DRIVER_USER_ERROR_1;
  }

  MemInfo->MemSize  = (uint8_t)IS25WX256_FLASH_SIZE_POW2; /* 2^25 = 32MB (256Mbit) */
  MemInfo->MpuCache = IS25WX256_MPU_CACHE_ATTR;

  return EXTMEM_DRIVER_USER_OK;
}

/**
  * @}
  */

/** @addtogroup USER_Private_Functions
  * @{
  */

/**
  * @}
  */

/**
  * @}
  */
#endif /* EXTMEM_DRIVER_USER == 1 */
