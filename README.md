本作品旨在设计一款基于 ESP12F微控制器的智能桌面风扇控制器，具有PWM无级调速、自然风模式、VFD指示屏、ARGB灯光、联网时间显示以及检测环境光自动调整亮度的功能
-（是我的通用技术作业）
-This project aims to design an intelligent desktop fan controller based on the ESP12F microcontroller, featuring PWM continuous speed control, natural wind mode, VFD display screen, ARGB lighting, online time display, and the function of automatically adjusting brightness based on ambient light detection. 
-(This is my general technical assignment)
<img width="2443" height="1739" alt="jpg" src="https://github.com/user-attachments/assets/8f02bc87-b9ea-46be-9498-ab48c817fc9a" />

【通用技术作业实录】 https://www.bilibili.com/video/BV1krV56mEhq/?share_source=copy_web&vd_source=b246e824253217c284212f61fc7883cc
-此为该项目制作过程，该项目为搭棚焊接，暂无PCB图，原理图展示了硬件链接。附件有模型和源代码供各位参考
-推荐使用VS code + platformio插件 进行开发和固件下载
-加qq群546926675 一起讨论吧

[General Technology Project Record] https://www.bilibili.com/video/BV1krV56mEhq/?share_source=copy_web&vd_source=b246e824253217c284212f61fc7883cc
-This is the production process of this project. The project involves building a shed and welding. There is no PCB diagram yet. The schematic diagram shows the hardware connections. The attachment includes models and source codes for your reference.
-It is recommended to use VS code + platformio plugin for development and firmware download.
-Join the QQ group 546926675 to discuss together.


-Design process:
-Initially, it was envisioned to create a modular 12CM fan controller with a built-in 21700 battery and a long strip LCD screen. However, due to time and safety considerations, it was changed to using the existing and mature VFD display and an external power supply solution.
The VFD display module was a project designed by myself in March this year. At that time, I wrote mature and usable ESP12F driver code, and the open-source project link is: https://oshwhub.com/git-key/mi-ni-vfd-shi-zhong. It has the advantages of high brightness and contrast.
The fan uses Liming S12RW. To ensure an elegant appearance, an anti-rotating ARGB fan was needed. Only this one is available on the market at a reasonable price. The buzzer design was cancelled due to the insufficient pins of ESP12F.
-EC11, push buttons, etc. are all existing materials, suitable for completing this project within one week. The shell was initially made by sandblasted PLA 3D printing, and later, a more resilient and weather-resistant sandblasted PETG was chosen for printing. 
-Aesthetics principle: The color scheme and shape design of this work are elegant. The connection parts of the shell are rounded, the color is beautiful, and the overall design style is minimalist. 
-Innovative principle: The desktop fan is made intelligent and aesthetically pleasing, and a VFD screen design that is not available in the market has been incorporated. 
-Sustainable development principle: All the solder wires, fluxes, etc. in this product are lead-free. The modules comply with the RHOS standard and do not cause heavy metal pollution. The 3D printing materials selected are non-toxic to humans and recyclable and reusable. 
-Safety principle: External power supply, avoiding the risk of battery fire. All circuit soldering points are reinforced with electrical insulation UV glue to ensure safety. 
-Cost Estimation: 
-Limin S12RW fan x 1 24.6
-8-MD-06INKM VFD display 23.5 
-EC11 module 5.2
-ESP12F 5.74
-Resistors, capacitors, inductors 3.28
-Boost IC, LDO, diodes 2.7
-3D printing material cost 1.74
-Total: 24.6 + 23.5 + 5.2 + 5.74 + 3.28 + 2.7 + 1.74 = 66.76 RMB 
-The structure of the work conforms to mechanics: it is compatible with the shear force and the 3D printing layered structure, and the design is exquisite. 
-It offers diverse functions and rich ARGB lighting colors. It is highly extensible. The main control can be connected to WiFi and synchronize time via NTP. In the future, it can also remotely display various contents through ESP now communication, becoming a mobile terminal. 
-The human-computer interaction logic has been thoroughly verified and the design is extremely reasonable. With just the EC11 encoder, one can select fan mode, control ARGB lights, set automatic brightness, and so on.The operation is fast and convenient. 
-Innovation aspect: It is the only open-source project on the market that incorporates a VFD screen into a portable fan, setting a precedent for handheld fans to serve as both decorative items and handheld terminals. 
-Regarding the process of creating this work, it has enhanced my ability to withstand setbacks: I re-welded the main control and the fan twice and made dozens of changes to the model and code. It has also improved my -creativity: I independently completed the entire process of design, welding, 3D modeling, 3D printing adjustment, and embedded programming in less than a week, which has led to significant growth in all aspects of my abilities. 
-Material selection: 1. The VFD display module is an open-source project designed by myself in March this year. At that time, I wrote mature and usable ESP12F driver code, and it has the advantages of high brightness and contrast. 
-2. Materials such as EC11 and microswitches are all available and suitable for completing this project within one week. The outer shell was initially 3D-printed using matte PLA, and later was replaced with a more resilient and weather-resistant matte PETG for printing. 
-3. The photodiode was an element purchased for the previous film camera shutter speed measurement project. Its voltage range of 0 - 600mv is exactly within the sampling range of the microcontroller's ADC. 
-4. The esp12f microcontroller is inexpensive, has Wi-Fi functionality, has a sufficient number of pins and an ADC. It is an excellent choice for this project. 
-5. The 12cm fan uses Liming S12RW. To ensure an elegant appearance, an anti-rotation ARGB fan is required. Only this one is available on the market at a reasonable price. Structure: This controller and the fan body are designed with an upper and lower stacking structure. The center of gravity is at the bottom, which is placed on the desktop for stable and reliable operation, with an elegant appearance. It is connected by a PETG shell and 304 stainless steel m5 15mm screws, providing excellent weather resistance. Function: 
-6. Functions: PWM continuous speed control, natural wind mode, VFD display screen, ARGB lighting, network time display, and automatic brightness adjustment based on ambient light detection. All the functions are included, and the device has excellent maintainability and scalability: USB D+D- pins are linked to the TX/RX interface of the main controller, allowing for program flashing at any time. It can also be remotely controlled via ESP now communication, becoming a mobile terminal. 


-The initial design was for a modular 12CM fan controller that incorporated a built-in 21700 battery and featured a long strip LCD screen. However, due to time constraints and battery safety considerations, it was changed to use the existing and mature VFD display and an external power supply solution. 
-The buzzer design was cancelled because the ESP12F had insufficient pins.


-设计过程：
-最初设想为内置一节21700电池、使用长条形LCD屏幕的模块化12CM风扇控制器，但因时间、安全等因素改为使用现有且成熟的VFD显示屏以及外部供电方案。
-VFD显示模组为今年3月本人自己设计的项目，当时编写了成熟可用的ESP12F驱动代码，开源项目链接：https://oshwhub.com/git-key/mi-ni-vfd-shi-zhong 且具有高亮度与对比度的优点。
-风扇采用利民S12RW ，为了确保外观设计优雅故需要反叶ARGB风扇，市面上价格合适的仅此一款。蜂鸣器设计由于ESP12F引脚不足，故取消。
-EC11与 微动按钮 等材料都是现有的，适合一周内完成该项目。外壳最初采用磨砂PLA 3D打印，后选用更有韧性和耐候性的磨砂PETG打印。

-美观性原则：该作品配色与外形设计优雅，外壳链接部位做圆角处理，配色美观，整体为极简主义设计风格。

-创新性原则：将桌面风扇智能化、美观化并加入了市面上没用的VFD屏幕设计。

-可持续发展原则：该作品焊锡丝、助焊剂等全部无铅，模块符合RHOS标准，不造成重金属污染。3D打印材料选用无人体危害且可回收重复利用的PETG。

-安全性原则：外部供电、避免电池起火危险，全部电路焊接点都使用电气绝缘UV胶加固，保证安全。

-成本估算：

-利民S12RW风扇 x 1 24.6
-8-MD-06INKM VFD显示屏 23.5

-EC11模块 5.2
-ESP12F 5.74
-电阻、电容、电感元件 3.28
-升压IC 、LDO 、二极管 2.7
-3D打印材料费 1.74
-总计：24.6+23.5+5.2+5.74+3.28+2.7+1.74= 66.76人民币

-作品结构符合力学：抗剪切力和3D打印结构层叠结构相符，设计精巧

-实现功能多样，ARGB灯光色彩丰富。可扩展新极佳，主控可连接WiFi ntp同步时间，未来更可通过ESP now通讯远程显示各种内容，成为移动化终端。

-人机交互逻辑通过反复验证设计的十分合理，仅通过EC11编码器就可以选择风扇模式、控制ARGB灯光、设置自动亮度等等，操作快捷方便。

-创新方面：为市面上唯一将VFD屏幕加入到便携风扇的开源作品，开创了手持风扇作为摆件和手持终端双功能的先例。

-对于制作该作品的过程，锻炼了本人的抗挫能力：重新焊接主控和风扇各两次、数十次更改模型和代码。提升了本人的创造能力：仅仅用时一周不到独立完成了设计、焊接、3D建模、3D打印调参、嵌入式编程的全过程，使得自己各方面都得到了显著成长。

-选材： 
-1.VFD显示模组为今年3月本人自己设计的开源项目，当时编写了成熟可用的ESP12F驱动代码， 且具有高亮度与对比度的优点。

-2.EC11与 微动按钮 等材料都是现有的，适合一周内完成该项目。外壳最初采用磨砂PLA 3D打印，后选用更有韧性和耐候性的磨砂PETG打印。

-3.光电二极管为之前胶片相机快门测速器项目购买的元件，其电压范围0-600mv正好在微控制器adc的采样范围之内。

-4.esp12f微控制器价格低廉，有wifi功能，引脚数量够多且拥有一个adc。是本项目的极佳选择。

-5. 12cm风扇采用利民S12RW ，为了确保外观设计优雅故需要反叶ARGB风扇，市面上价格合适的仅此一款。 结构：本控制器与风扇主体采用上下堆叠设计，重心在下摆放在桌面稳定可靠，外形优雅。使用PETG外壳和304不锈钢m5 15mm螺丝连接件，耐候性极佳。 功

-6.功能：PWM无级调速、自然风模式、VFD指示屏、ARGB灯光、联网时间显示以及检测环境光自动调整亮度。功能应有尽有，且可维护性和扩展性极佳：usb d+d-引脚链接主控的TX/RX接口，可随时刷写程序。更可通过ESP now通讯远程控制，成为移动化终端。

-最初设想为内置一节21700电池、使用长条形LCD屏幕的模块化12CM风扇控制器，但因时间、电池安全等因素改为使用现有且成熟的VFD显示屏以及外部供电方案。

-蜂鸣器设计由于ESP12F引脚不足，故取消。
