*Disclaimer:* This is not a full integration of FEMFX into the Unreal Physics system.

# AMD FEMFX plugin

*AMD FEMFX plugin* is an Unreal Engine 4 is a UE4 game plugin based on the AMD FEMFX multi-threaded CPU library. The FEMFX library utilizes the finite element method to realistically simulate deformation of objects with different material properties.
For more information about the FEMFX library, please visit: [FEMFX Page](https://github.com/GPUOpen-Effects/FEMFX)


### Prerequisites
* Unreal Engine version 4.18
* Visual Studio&reg; 2017.
* AMD Ryzen™ 7 2700X Processor or equivalent


### Getting Started
* Make sure the Unreal 4.18 source code is already on your system
* Download the plugin and double click the project file to build the plugin and the dependency modules
	
	
The editor built based on the above instructions will have the FEM plugin enabled. For more information on creating and importing models into Unreal for real-time FEM simulation, please refer to [FEMFX Page](https://github.com/GPUOpen-Effects/FEMFX)

### Building the test content with FEM plugin

* Download the content 
* Copy the "Plugins" folder in the same directory as your game project 
* Double click on the project file to build the game with FEM effects