/**
  ******************************************************************************
  * @file    is25wx256.h
  * @author  User Driver
  * @brief   ISSI IS25WX256 / IS25WX128 Octal xSPI Flash memory definitions.
  *          Commands, register maps and timing values are taken from the
  *          ISSI "IS25LX256/128 IS25WX256/128" datasheet, Rev. A14 (05/12/2026).
  ******************************************************************************
  * @attention
  *
  * This file only contains memory-specific definitions (opcodes, register
  * layouts, timing parameters). It has no dependency on the STM32 HAL and can
  * be reused with any host controller.
  *
  ******************************************************************************
  */

#ifndef IS25WX256_H
#define IS25WX256_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================================================================
 * Device identification (Table 7.1)
 * ==========================================================================*/
#define IS25WX256_MANUFACTURER_ID            0x9Du
#define IS25WX256_MEMORY_TYPE_1V8             0x5Bu  /* IS25WX (1.8V) family */
#define IS25WX256_MEMORY_DENSITY_256MB        0x19u  /* 256 Mbit */
#define IS25WX256_MEMORY_DENSITY_128MB        0x18u  /* 128 Mbit */

/* ============================================================================
 * Memory geometry
 * ==========================================================================*/
#define IS25WX256_FLASH_SIZE                 (0x2000000U)   /* 256 Mbit = 32 MByte */
#define IS25WX128_FLASH_SIZE                 (0x1000000U)   /* 128 Mbit = 16 MByte */

/* Memory size expressed as a power of 2 (2^25 = 32MB, 2^24 = 16MB), as
   expected by EXTMEM_USER_MemInfoTypeDef.MemSize. */
#define IS25WX256_FLASH_SIZE_POW2            (25U)          /* 2^25 = 32MB (256Mbit) */
#define IS25WX128_FLASH_SIZE_POW2            (24U)          /* 2^24 = 16MB (128Mbit) */

#define IS25WX256_PAGE_SIZE                  (256U)         /* Page Program 1..256B  (8.11) */
#define IS25WX256_SUBSECTOR_4K_SIZE          (4U  * 1024U)  /* 4KB  subsector */
#define IS25WX256_SUBSECTOR_32K_SIZE         (32U * 1024U)  /* 32KB subsector */
#define IS25WX256_SECTOR_SIZE                (128U * 1024U) /* 128KB sector (default, Table 5.1) */

#define IS25WX256_OTP_SIZE                   (64U)          /* Dedicated 64-byte OTP area, 8.14 */

/* ============================================================================
 * Command set (Table 8.1) -- Octal DDR (8D-8D-8D) opcodes are identical to
 * the Extended SPI ones; protocol selection is done by the host controller
 * (instruction sent as 2-byte DDR "valid byte + repeated byte").
 * ==========================================================================*/

/* --- Software RESET Operations (8.3) --- */
#define IS25WX256_CMD_RESET_ENABLE           0x66u
#define IS25WX256_CMD_RESET_MEMORY           0x99u

/* --- READ ID Operations (8.4) --- */
#define IS25WX256_CMD_READ_ID                0x9Fu   /* 9E/9Fh */

/* --- READ SFDP (8.5) --- */
#define IS25WX256_CMD_READ_SFDP              0x5Au

/* --- READ MEMORY, 3/4-byte address (8.6) --- */
#define IS25WX256_CMD_READ                   0x03u
#define IS25WX256_CMD_FAST_READ              0x0Bu
#define IS25WX256_CMD_OCTAL_OUT_FAST_READ    0x8Bu
#define IS25WX256_CMD_OCTAL_IO_FAST_READ     0xCBu
#define IS25WX256_CMD_DDR_OCTAL_OUT_FAST_READ 0x9Du

/* --- READ MEMORY, 4-byte address fixed (8.6) --- */
#define IS25WX256_CMD_4B_READ                0x13u
#define IS25WX256_CMD_4B_FAST_READ           0x0Cu
#define IS25WX256_CMD_4B_OCTAL_OUT_FAST_READ 0x7Cu
#define IS25WX256_CMD_4B_OCTAL_IO_FAST_READ  0xCCu
#define IS25WX256_CMD_DDR_OCTAL_IO_FAST_READ 0xFDu /* 4-byte address only, 1S-8D-8D / used as 8D-8D-8D in Octal DDR mode */

/* --- WRITE Operations (8.7) --- */
#define IS25WX256_CMD_WRITE_ENABLE           0x06u
#define IS25WX256_CMD_WRITE_DISABLE          0x04u

/* --- READ REGISTER Operations (8.8) --- */
#define IS25WX256_CMD_READ_STATUS_REG        0x05u
#define IS25WX256_CMD_READ_FLAG_STATUS_REG   0x70u
#define IS25WX256_CMD_READ_NV_CFG_REG        0xB5u
#define IS25WX256_CMD_READ_V_CFG_REG         0x85u
#define IS25WX256_CMD_READ_PROT_MGMT_REG     0x2Bu

/* --- WRITE REGISTER Operations (8.9) --- */
#define IS25WX256_CMD_WRITE_STATUS_REG       0x01u
#define IS25WX256_CMD_WRITE_NV_CFG_REG       0xB1u
#define IS25WX256_CMD_WRITE_V_CFG_REG        0x81u
#define IS25WX256_CMD_WRITE_PROT_MGMT_REG    0x68u

/* --- CLEAR Operations (8.10 / 8.24) --- */
#define IS25WX256_CMD_CLEAR_FLAG_STATUS_REG  0x50u
#define IS25WX256_CMD_CLEAR_ERRB             0xB6u

/* --- PROGRAM Operations, 3/4-byte address (8.11) --- */
#define IS25WX256_CMD_PAGE_PROGRAM           0x02u
#define IS25WX256_CMD_OCTAL_IN_FAST_PROGRAM  0x82u
#define IS25WX256_CMD_EXT_OCTAL_IN_FAST_PROG 0xC2u

/* --- PROGRAM Operations, 4-byte address fixed (8.11) --- */
#define IS25WX256_CMD_4B_PAGE_PROGRAM           0x12u
#define IS25WX256_CMD_4B_OCTAL_IN_FAST_PROGRAM  0x84u
#define IS25WX256_CMD_4B_EXT_OCTAL_IN_FAST_PROG 0x8Eu

/* --- ERASE Operations, 3/4-byte address (8.12) --- */
#define IS25WX256_CMD_SUBSECTOR_ERASE_32K    0x52u
#define IS25WX256_CMD_SUBSECTOR_ERASE_4K     0x20u
#define IS25WX256_CMD_SECTOR_ERASE_128K      0xD8u
#define IS25WX256_CMD_CHIP_ERASE              0x60u  /* C7h/60h, equivalent */

/* --- ERASE Operations, 4-byte address fixed (8.12) --- */
#define IS25WX256_CMD_4B_SUBSECTOR_ERASE_32K 0x5Cu
#define IS25WX256_CMD_4B_SUBSECTOR_ERASE_4K  0x21u
#define IS25WX256_CMD_4B_SECTOR_ERASE_128K   0xDCu

/* --- SUSPEND/RESUME Operations (8.13) --- */
#define IS25WX256_CMD_PROG_ERASE_SUSPEND     0x75u
#define IS25WX256_CMD_PROG_ERASE_RESUME      0x7Au

/* --- ONE-TIME-PROGRAMMABLE Operations (8.14/8.15) --- */
#define IS25WX256_CMD_READ_OTP_ARRAY         0x4Bu
#define IS25WX256_CMD_PROGRAM_OTP_ARRAY      0x42u

/* --- 4-BYTE ADDRESS MODE Operations (8.16) --- */
#define IS25WX256_CMD_ENTER_4B_ADDR_MODE     0xB7u
#define IS25WX256_CMD_EXIT_4B_ADDR_MODE      0xE9u

/* --- DEEP POWER-DOWN Operations --- */
#define IS25WX256_CMD_ENTER_DEEP_POWER_DOWN  0xB9u
#define IS25WX256_CMD_RELEASE_DEEP_POWER_DOWN 0xABu

/* --- ADVANCED SECTOR PROTECTION (ASP) Operations --- */
#define IS25WX256_CMD_READ_SECTOR_PROTECTION    0x2Du
#define IS25WX256_CMD_PROGRAM_SECTOR_PROTECTION 0x2Cu
#define IS25WX256_CMD_READ_VOLATILE_LOCK_BITS   0xE8u
#define IS25WX256_CMD_WRITE_VOLATILE_LOCK_BITS  0xE5u
#define IS25WX256_CMD_READ_NV_LOCK_BITS         0xE2u
#define IS25WX256_CMD_WRITE_NV_LOCK_BITS        0xE3u
#define IS25WX256_CMD_ERASE_NV_LOCK_BITS        0xE4u
#define IS25WX256_CMD_READ_GLOBAL_FREEZE_BIT    0xA7u
#define IS25WX256_CMD_WRITE_GLOBAL_FREEZE_BIT   0xA6u
#define IS25WX256_CMD_READ_PASSWORD             0x27u
#define IS25WX256_CMD_WRITE_PASSWORD            0x28u
#define IS25WX256_CMD_UNLOCK_PASSWORD           0x29u
#define IS25WX256_CMD_4B_READ_VOLATILE_LOCK_BITS  0xE0u
#define IS25WX256_CMD_4B_WRITE_VOLATILE_LOCK_BITS 0xE1u

/* --- DATA LEARNING PATTERN (8.20) --- */
#define IS25WX256_CMD_DLPRD                  0xCDu

/* ============================================================================
 * Status Register (6.1, Table 6.1)
 * ==========================================================================*/
#define IS25WX256_SR_WIP                     (1u << 0)  /* Write In Progress (Busy) */
#define IS25WX256_SR_WEL                     (1u << 1)  /* Write Enable Latch */
#define IS25WX256_SR_BP0                     (1u << 2)
#define IS25WX256_SR_BP1                     (1u << 3)
#define IS25WX256_SR_BP2                     (1u << 4)
#define IS25WX256_SR_TB                      (1u << 5)
#define IS25WX256_SR_BP3                     (1u << 6)
#define IS25WX256_SR_SRWD                    (1u << 7)

/* ============================================================================
 * Flag Status Register (6.2, Table 6.4)
 * ==========================================================================*/
#define IS25WX256_FSR_ADDRESSING             (1u << 0)  /* 0=3-byte, 1=4-byte */
#define IS25WX256_FSR_PROTECTION             (1u << 1)  /* protection error */
#define IS25WX256_FSR_PROGRAM_SUSPEND        (1u << 2)
#define IS25WX256_FSR_RESERVED               (1u << 3)
#define IS25WX256_FSR_PROGRAM_ERROR          (1u << 4)
#define IS25WX256_FSR_ERASE_ERROR            (1u << 5)
#define IS25WX256_FSR_ERASE_SUSPEND          (1u << 6)
#define IS25WX256_FSR_READY                  (1u << 7)  /* 1 = controller ready (not busy) */

/* ============================================================================
 * Nonvolatile / Volatile Configuration Register byte map (Table 6.5 / 6.6)
 * Addressed through the LSB of the (main array) address field.
 * ==========================================================================*/
#define IS25WX256_CR_ADDR_IO_MODE            0x00u
#define IS25WX256_CR_ADDR_DUMMY_CYCLES       0x01u
#define IS25WX256_CR_ADDR_RESERVED_02        0x02u
#define IS25WX256_CR_ADDR_OUTPUT_DRIVE       0x03u
#define IS25WX256_CR_ADDR_RESERVED_04        0x04u
#define IS25WX256_CR_ADDR_4BYTE_ADDR_CFG     0x05u
#define IS25WX256_CR_ADDR_XIP_CFG            0x06u
#define IS25WX256_CR_ADDR_WRAP_CFG           0x07u
#define IS25WX256_CR_ADDR_ECC_CFG            0x0Bu  /* SSOENB/CRCSIZE/CRCENB/ERRBECC/ERRBENB/ECCENB */
#define IS25WX256_CR_ADDR_DLP_PATTERN        0x0Au

/* I/O mode byte (address 00h) values (Table 6.5) */
#define IS25WX256_IOMODE_EXTENDED_SPI        0xFFu  /* default */
#define IS25WX256_IOMODE_EXTENDED_SPI_NO_DQS 0xDFu
#define IS25WX256_IOMODE_OCTAL_DDR           0xE7u
#define IS25WX256_IOMODE_OCTAL_DDR_NO_DQS    0xC7u

/* 4-byte address configuration byte (address 05h) */
#define IS25WX256_ADDR_MODE_3BYTE            0xFFu  /* default */
#define IS25WX256_ADDR_MODE_4BYTE            0xFEu

/* Dummy cycle configuration byte (address 01h): 1Fh = default (datasheet 8.6 note) */
#define IS25WX256_DUMMY_CYCLES_DEFAULT       0x1Fu

/* ECC/CRC configuration register bits (address 0Bh, Table 6.5/6.6) */
#define IS25WX256_CR0B_ECCENB                (1u << 0)  /* 0 = ECC ON, 1 = ECC OFF (default) */
#define IS25WX256_CR0B_ERRBENB               (1u << 1)  /* 0 = ERR# ON, 1 = ERR# OFF (default) */
#define IS25WX256_CR0B_ERRBECC                (1u << 2)
#define IS25WX256_CR0B_CRCENB                (1u << 3)  /* 0 = CRC enabled, 1 = CRC disabled (default) */
#define IS25WX256_CR0B_CRCSIZE_MASK          (3u << 4)
#define IS25WX256_CR0B_SSOENB                (1u << 6)

/* ============================================================================
 * Volatile Configuration Register - ECC status (address 0Ch, Table 6.6)
 * ==========================================================================*/
#define IS25WX256_VCR0C_CRCSTAT              (1u << 0)
#define IS25WX256_VCR0C_PARSTAT              (1u << 1)
#define IS25WX256_VCR0C_ECCSTAT              (1u << 2)
#define IS25WX256_VCR0C_ECCCOUNTER_MASK      (0x0Fu << 3)
#define IS25WX256_VCR0C_IPA_ECCB             (1u << 7)

/* ============================================================================
 * Device timing characteristics (Table 9.3, VCC=1.7-1.95V, DDR=200MHz,
 * ECC OFF) -- used for software timeouts (values are Max from datasheet,
 * rounded up with margin).
 * ==========================================================================*/
#define IS25WX256_TIMEOUT_WRSR_MS            (15U)    /* tW max */
#define IS25WX256_TIMEOUT_WRNVCR_MS          (1000U)  /* tWNVCR max (1s) */
#define IS25WX256_TIMEOUT_PAGE_PROGRAM_MS    (5U)     /* tPP max 1.8ms, margin */
#define IS25WX256_TIMEOUT_SUBSECTOR_4K_MS    (400U)   /* tSE4K max */
#define IS25WX256_TIMEOUT_SUBSECTOR_32K_MS   (1000U)  /* tSE32K max */
#define IS25WX256_TIMEOUT_SECTOR_128K_MS     (1000U)  /* tSE max */
#define IS25WX256_TIMEOUT_CHIP_ERASE_MS      (200000U)/* tBE max ~180s for 256Mb, margin */
#define IS25WX256_TIMEOUT_OTP_MS             (1U)     /* tOTP max 0.8ms, margin */

/* Minimum dummy cycles needed for DDR Octal I/O Fast Read at 200MHz (ECC
   OFF) per Table 6.7: 20 dummy cycles -> 200MHz. We use the NVCR default
   (1Fh = 30 cycles, see 8.6 note 1) which safely covers every supported
   frequency / command in this driver. */
#define IS25WX256_FAST_READ_DUMMY_CYCLES     (20U)

#ifdef __cplusplus
}
#endif

#endif /* IS25WX256_H */
