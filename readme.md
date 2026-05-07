# ELEC1620 Unit 4 - Lab 4 - Assessment Template



The most important file is `main.c` which is found in `Core/Src/main.c` 



## Setup

Make sure you open the folder that contains:

Core
Drivers
CMakeLists.txt

When prompted:

"Would you like to configure discovered CMake project as STM32Cube project?"

Select Yes.

Choose the Debug configuration if prompted

Click Build to check the project compiles

Check the board is detected under "STM32CUBE Devices and Boards"

Click Run and Debug

Select STM32Cube: STLink GDB Server

Press F5 (or the green arrow) to run past breakpoints


## Troubleshooting

**Board not detected:**
- Ensure the ST-Link drivers are installed
- Check the USB cable is properly connected (use the USB connector labeled "USB ST-LINK")
- Try a different USB port or cable
- Check under "STM32CUBE Devices and Boards" in the Run and Debug page

**Build errors:**
- Make sure you selected "Yes" when asked to configure the project as an STM32Cube project
- Try cleaning the build folder and rebuilding
- Check that the Debug configuration is selected

