/**
  ******************************************************************************
  * @file    stm32_user_driver_conf.h
  * @author  User Driver
  * @brief   Project-specific configuration for the IS25WX256 USER driver.
  *          Edit this file to match your hardware (XSPI instance used,
  *          number of bytes used for the 4-byte address, etc.)
  ******************************************************************************
  */

#ifndef STM32_USER_DRIVER_CONF_H
#define STM32_USER_DRIVER_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32n6xx_hal.h"

/*
 * XSPI instance connected to the IS25WX256.
 * On NUCLEO-N657X0-Q / STM32N6570-DK the external octal flash is generally
 * wired on XSPI1 or XSPI2 -- adjust according to your schematic.
 */
extern XSPI_HandleTypeDef hxspi2;
#define IS25WX256_XSPI_HANDLE                 (&hxspi2)

/* Memory-mapped base address for this XSPI instance (see Reference Manual,
   memory map chapter -- adjust to the instance actually used). */
#define IS25WX256_XSPI_MM_BASE_ADDRESS        (0x90000000U)

/* Set to 1 if the board wires the flash so that 4-byte addressing must be
   used even below 128Mbit (recommended: always keep at 1, since Octal DDR
   protocol on this device is always 4-byte address, see datasheet 4.). */
#define IS25WX256_FORCE_4BYTE_ADDRESS         (1)

/*
 * MPU cache attribute reported through EXTMEM_USER_MemInfoTypeDef.MpuCache
 * for the memory-mapped XSPI region. This value is project/MPU-policy
 * specific (it is NOT documented in the flash datasheet), so it is left
 * here as a single edit point.
 *
 * On Cortex-M55 (STM32N6) typical choices, using the CMSIS MPU helper
 * macros from core_armv8mml.h / ARM_MPU_ARMv8.h, are for example:
 *   - Normal memory, Write-Through, Read-allocate, no Write-allocate:
 *       ARM_MPU_ATTR_MEMORY_(1,0,1,0)
 *   - Normal memory, Non-cacheable (safe default if XIP execution
 *     coherency with the data cache has not been validated yet):
 *       ARM_MPU_ATTR_MEMORY_(0,0,0,0)
 *
 * Adjust to match the MPU attribute index/table actually configured by
 * your application (see MPU_ConfigMemoryAttributes() / ARM_MPU_SetMemAttr()
 * in your BSP). A plain non-cacheable default is used below to stay safe
 * out of the box; raise it once cache-maintenance around Erase/Program is
 * validated for your use case.
 */
#define IS25WX256_MPU_CACHE_ATTR              (0x00000000U) /* Non-cacheable, safe default */

#ifdef __cplusplus
}
#endif

#endif /* STM32_USER_DRIVER_CONF_H */
