.. zephyr:code-sample:: blinky
   :name: Blinky
   :relevant-api: gpio_interface

   Blink an LED forever using the GPIO API.

Overview
********

The Blinky sample blinks an LED forever using the :ref:`GPIO API <gpio_api>`.

The source code shows how to:

#. Get a pin specification from the :ref:`devicetree <dt-guide>` as a
   :c:struct:`gpio_dt_spec`
#. Configure the GPIO pin as an output
#. Toggle the pin forever

See :zephyr:code-sample:`pwm-blinky` for a similar sample that uses the PWM API instead.

.. _blinky-sample-requirements:

Requirements
************

Your board must:

#. Have an LED connected via a GPIO pin (these are called "User LEDs" on many of
   Zephyr's :ref:`boards`).
#. Have the LED configured using the ``led0`` devicetree alias.

Building and Running
********************

Build and flash Blinky for the Seeed Studio XIAO nRF52840 Sense using the provided ``manage_device.sh`` script. This script acts as a universally reusable tool for managing Zephyr builds and flashing via the XIAO Debug Mate (CMSIS-DAP debugger).

First, make exactly sure the script is executable:

.. code-block:: console

   $ chmod +x manage_device.sh

Usage of ``manage_device.sh``:

- To build the application:
  ``./manage_device.sh build``

- To flash it to the XIAO nRF52840:
  ``./manage_device.sh flash``

- To build and flash the application in one step:
  ``./manage_device.sh run``

- To start an interactive debug session via GDB:
  ``./manage_device.sh debug``

- To configure VS Code for visual debugging (Cortex-Debug):
  ``./manage_device.sh setup-debug``

- To clean the build directory:
  ``./manage_device.sh clean``

Visual Debugging with VS Code
=============================

The ``setup-debug`` command auto-configures everything needed for visual debugging
in VS Code using the `Cortex-Debug <https://marketplace.visualstudio.com/items?itemName=marus25.cortex-debug>`_ extension.

Run it once (or any time your environment changes):

.. code-block:: console

   $ ./manage_device.sh setup-debug

This will:

#. **Find a working ARM GDB** — searches Zephyr SDK installations under ``$HOME``,
   then ``gdb-multiarch`` and ``arm-none-eabi-gdb`` on ``$PATH``. Snap-confined
   binaries are automatically skipped because they lack the network access needed
   to connect to OpenOCD.
#. **Locate the built ELF** — checks both sysbuild (``build/*/zephyr/zephyr.elf``)
   and standard (``build/zephyr/zephyr.elf``) layouts.
#. **Derive the OpenOCD target** from the ``BOARD`` variable (supports nRF52, nRF53,
   nRF91, and STM32 families out of the box).
#. **Install udev rules** if the project contains ``60-openocd.rules`` and it has not
   yet been copied to ``/etc/udev/rules.d/`` (requires ``sudo``).
#. **Kill stale OpenOCD processes** that would block the GDB server port.
#. **Generate** ``.vscode/launch.json`` with all the correct, machine-specific paths.

After the command completes, press **F5** in VS Code to start a debug session.

The command is portable — it works in any Zephyr project directory and adapts to
the local toolchain installation. Override the board with:

.. code-block:: console

   $ BOARD=nrf52840dk_nrf52840 ./manage_device.sh setup-debug

After flashing, the LED starts to blink and messages with the current LED state
are printed on the console. If a runtime error occurs, the sample exits without
printing to the console.

Build errors
************

You will see a build error at the source code line defining the ``struct
gpio_dt_spec led`` variable if you try to build Blinky for an unsupported
board.

On GCC-based toolchains, the error looks like this:

.. code-block:: none

   error: '__device_dts_ord_DT_N_ALIAS_led_P_gpios_IDX_0_PH_ORD' undeclared here (not in a function)

Adding board support
********************

To add support for your board, add something like this to your devicetree:

.. code-block:: DTS

   / {
   	aliases {
   		led0 = &myled0;
   	};

   	leds {
   		compatible = "gpio-leds";
   		myled0: led_0 {
   			gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
                };
   	};
   };

The above sets your board's ``led0`` alias to use pin 13 on GPIO controller
``gpio0``. The pin flags :c:macro:`GPIO_ACTIVE_HIGH` mean the LED is on when
the pin is set to its high state, and off when the pin is in its low state.

Tips:

- See :dtcompatible:`gpio-leds` for more information on defining GPIO-based LEDs
  in devicetree.

- If you're not sure what to do, check the devicetrees for supported boards which
  use the same SoC as your target. See :ref:`get-devicetree-outputs` for details.

- See :zephyr_file:`include/zephyr/dt-bindings/gpio/gpio.h` for the flags you can use
  in devicetree.

- If the LED is built in to your board hardware, the alias should be defined in
  your :ref:`BOARD.dts file <devicetree-in-out-files>`. Otherwise, you can
  define one in a :ref:`devicetree overlay <set-devicetree-overlays>`.
