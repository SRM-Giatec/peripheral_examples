/**************************************************************************//**
 * @file main.c
 * @brief This project demonstrates the master configuration of the
 * EFx32xG21 I2C peripheral. Two EFx32xG21 modules are connected together, one
 * running the master project, the other running the slave project. The master
 * starts up and enters a while loop waiting for a button press on push button 0.
 * When push button 0 is pressed, the program performs an I2C test. This routine
 * reads the slave device's current buffer values, increments each value by 1,
 * and writes the new values back to the slave device. The master then reads back
 * the slave values again and verifies the new values match what was previously
 * written. Upon a successful write, LED0 is toggled and the device re-enters the
 * while loop, waiting again for user input through push button 0. This project
 * runs in a continuous loop, re-running the I2C test with every PB0 button press
 * and toggling LED0 with each successful iteration. If there is an I2C
 * transmission error, or if the verification step of the I2C test fails, LED1 is
 * turned on and the master sits and remains in an infinite while loop. Connecting
 * to the device via debugger while in the infinite loop, the I2C error code can
 * be retrieved.
 * @version 0.0.1
 ******************************************************************************
 * @section License
 * <b>Copyright 2018 Silicon Labs, Inc. http://www.silabs.com</b>
 *******************************************************************************
 *
 * This file is licensed under the Silabs License Agreement. See the file
 * "Silabs_License_Agreement.txt" for details. Before using this software for
 * any purpose, you must agree to the terms of that agreement.
 *
 ******************************************************************************/
 
#include <stdio.h>
#include "em_device.h"
#include "em_chip.h"
#include "em_i2c.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "em_gpio.h"
#include "bsp.h"

// Defines
#define I2C_SLAVE_ADDRESS               0xE2
#define I2C_TXBUFFER_SIZE                 10
#define I2C_RXBUFFER_SIZE                 10

// Buffers
uint8_t i2c_txBuffer[I2C_TXBUFFER_SIZE];
uint8_t i2c_rxBuffer[I2C_RXBUFFER_SIZE];

// Transmission flags
volatile bool i2c_startTx;

/**************************************************************************//**
 * @brief GPIO initialization
 *****************************************************************************/
void initGPIO(void)
{
  // Configure PB0 as input and interrupt
  GPIO_PinModeSet(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, gpioModeInputPull, 1);
  GPIO_ExtIntConfig(BSP_GPIO_PB0_PORT, BSP_GPIO_PB0_PIN, 0, false, true, true);

  // Configure LED0 and LED1 as output
  GPIO_PinModeSet(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN, gpioModePushPull, 0);

  /* Enable EVEN interrupt to catch button press that changes slew rate */
  NVIC_ClearPendingIRQ(GPIO_EVEN_IRQn);
  NVIC_EnableIRQ(GPIO_EVEN_IRQn);
}

/**************************************************************************//**
 * @brief  Setup I2C
 *****************************************************************************/
void initI2C(void)
{
  // Using default settings
  I2C_Init_TypeDef i2cInit = I2C_INIT_DEFAULT;

  // Using PA5 (SDA) and PA6 (SCL)
  GPIO_PinModeSet(gpioPortA, 5, gpioModeWiredAndPullUpFilter, 1);
  GPIO_PinModeSet(gpioPortA, 6, gpioModeWiredAndPullUpFilter, 1);

  // Route GPIO pins to I2C module
  GPIO->I2CROUTE[0].SDAROUTE = (GPIO->I2CROUTE[0].SDAROUTE & ~_GPIO_I2C_SDAROUTE_MASK)
                        | (gpioPortA << _GPIO_I2C_SDAROUTE_PORT_SHIFT
                        | (5 << _GPIO_I2C_SDAROUTE_PIN_SHIFT));
  GPIO->I2CROUTE[0].SCLROUTE = (GPIO->I2CROUTE[0].SCLROUTE & ~_GPIO_I2C_SCLROUTE_MASK)
                        | (gpioPortA << _GPIO_I2C_SCLROUTE_PORT_SHIFT
                        | (6 << _GPIO_I2C_SCLROUTE_PIN_SHIFT));
  GPIO->I2CROUTE[0].ROUTEEN = GPIO_I2C_ROUTEEN_SDAPEN | GPIO_I2C_ROUTEEN_SCLPEN;

  // Initializing the I2C
  I2C_Init(I2C0, &i2cInit);

  // Setting the status flags and index
  i2c_startTx = false;

  I2C0->CTRL = I2C_CTRL_AUTOSN;
}

/***************************************************************************//**
 * @brief I2C read numBytes from slave device starting at target address
 ******************************************************************************/
void I2C_MasterRead(uint16_t slaveAddress, uint8_t targetAddress, uint8_t *rxBuff, uint8_t numBytes)
{
  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;

  // Initializing I2C transfer
  i2cTransfer.addr          = slaveAddress;
  i2cTransfer.flags         = I2C_FLAG_WRITE_READ; // must write target address before reading
  i2cTransfer.buf[0].data   = &targetAddress;
  i2cTransfer.buf[0].len    = 1;
  i2cTransfer.buf[1].data   = rxBuff;
  i2cTransfer.buf[1].len    = numBytes;

  result = I2C_TransferInit(I2C0, &i2cTransfer);

  // Sending data
  while (result == i2cTransferInProgress)
  {
    result = I2C_Transfer(I2C0);
  }

  if(result != i2cTransferDone)
  {
    // LED1 ON and infinite while loop to indicate I2C transmission problem
    GPIO_PinOutSet(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN);
    while(1);
  }
}

/***************************************************************************//**
 * @brief I2C write numBytes to slave device starting at target address
 ******************************************************************************/
void I2C_MasterWrite(uint16_t slaveAddress, uint8_t targetAddress, uint8_t *txBuff, uint8_t numBytes)
{
  // Transfer structure
  I2C_TransferSeq_TypeDef i2cTransfer;
  I2C_TransferReturn_TypeDef result;
  uint8_t txBuffer[I2C_TXBUFFER_SIZE + 1];

  txBuffer[0] = targetAddress;
  for(int i = 0; i < numBytes; i++)
  {
      txBuffer[i + 1] = txBuff[i];
  }

  // Initializing I2C transfer
  i2cTransfer.addr          = slaveAddress;
  i2cTransfer.flags         = I2C_FLAG_WRITE;
  i2cTransfer.buf[0].data   = txBuffer;
  i2cTransfer.buf[0].len    = numBytes + 1;
  i2cTransfer.buf[1].data   = NULL;
  i2cTransfer.buf[1].len    = 0;

  result = I2C_TransferInit(I2C0, &i2cTransfer);

  // Sending data
  while (result == i2cTransferInProgress)
  {
    result = I2C_Transfer(I2C0);
  }

  if(result != i2cTransferDone)
  {
    // LED1 ON and infinite while loop to indicate I2C transmission problem
    GPIO_PinOutSet(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN);
    while(1);
  }
}

/**************************************************************************//**
 * @brief  I2C Read/Increment/Write/Verify
 *****************************************************************************/
bool testI2C(void)
{
  int i;
  bool I2CWriteVerify;

  // Initial read of bytes from slave
  I2C_MasterRead(I2C_SLAVE_ADDRESS, 0, i2c_rxBuffer, I2C_RXBUFFER_SIZE);

  // Increment received values and prepare to write back to slave
  for(i = 0; i < I2C_RXBUFFER_SIZE; i++)
  {
    i2c_txBuffer[i] = i2c_rxBuffer[i] + 1;
  }

  // Block write new values to slave
  I2C_MasterWrite(I2C_SLAVE_ADDRESS, 0, i2c_txBuffer, I2C_TXBUFFER_SIZE);

  // Block read from slave
  I2C_MasterRead(I2C_SLAVE_ADDRESS, 0, i2c_rxBuffer, I2C_RXBUFFER_SIZE);

  // Verify I2C transmission
  I2CWriteVerify = true;
  for(i = 0; i < I2C_RXBUFFER_SIZE; i++)
  {
    if(i2c_txBuffer[i] != i2c_rxBuffer[i])
    {
      I2CWriteVerify = false;
      break;
    }
  }

  return I2CWriteVerify;
}

/***************************************************************************//**
 * @brief GPIO Interrupt handler
 ******************************************************************************/
void GPIO_EVEN_IRQHandler(void)
{
  // Clear pending
  uint32_t interruptMask = GPIO_IntGet();
  GPIO_IntClear(interruptMask);

  // re-enable I2C
  I2C_Enable(I2C0, true);

  i2c_startTx = true;
}

/**************************************************************************//**
 * @brief  Main function
 *****************************************************************************/
int main(void)
{
  // Chip errata
  CHIP_Init();

  // Initializations
  initGPIO();

  // Setting up i2c
  initI2C();

  // Main program loop
  while(1)
  {
    if (i2c_startTx)
    {
      // Transmitting data
      if(testI2C() == false)
      {
        // indicate error with LED1
        GPIO_PinOutSet(BSP_GPIO_LED1_PORT, BSP_GPIO_LED1_PIN);

        // sit in infinite while loop
        while(1);
      } else
      {
        // toggle LED0 with each pass
        GPIO_PinOutToggle(BSP_GPIO_LED0_PORT, BSP_GPIO_LED0_PIN);

        // Transmission complete
        i2c_startTx = false;
      }
    }
  }
}
