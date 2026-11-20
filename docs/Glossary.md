# Glossary

## CANopen

| Term                   | Meaning                                                                                                                       |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| **CANopen**            | Protocol on top of CAN that defines devices, an object dictionary and services to access it. CiA 301 is the base standard.   |
| **CiA 402**            | CANopen profile for drives and motion control: the state machine, controlword, statusword and operating modes.               |
| **Object dictionary**  | A device's table of parameters and values, addressed by a 16-bit index and an 8-bit sub-index, written `6040` or `2008:01`. |
| **Node ID**            | A device's address on the bus, 1–127. NiMotion drives default to 1.                                                          |
| **NMT**                | Network management: commands to start, stop and reset nodes (`0x01` start, `0x80` pre-operational, `0x81` reset application). |
| **SDO**                | Service data object: request/response read or write of one dictionary entry. Request on `0x600+id`, response on `0x580+id`.  |
| **Expedited SDO**      | An SDO transfer of up to 4 bytes in a single frame.                                                                           |
| **Segmented SDO**      | An SDO transfer of more than 4 bytes over several frames, used for strings.                                                   |
| **SDO abort code**     | 32-bit error a device returns when an SDO fails, such as "object does not exist" or "value too high".                        |
| **PDO**                | Process data object: unconfirmed frames carrying mapped values, sent by a device (TPDO) or to it (RPDO), for live data.      |
| **EMCY**               | Emergency message a device sends on `0x80+id` when an error occurs.                                                           |

## CiA 402

| Term                     | Meaning                                                                                                                 |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------- |
| **Controlword**          | `6040`. Commands the state machine (shutdown, switch on, enable operation, fault reset) and mode-specific bits.         |
| **Statusword**           | `6041`. Reports state and flags such as target reached, speed zero, homing done.                                       |
| **State walk**           | Switch On Disabled → Ready To Switch On → Switched On → Operation Enabled, one controlword step at a time.              |
| **PV (Profile Velocity)** | Mode 3. The drive runs at a target velocity with a configured acceleration.                                            |
| **PP (Profile Position)** | Mode 1. The drive moves to a target position with configured speed and acceleration.                                   |
| **HM (Homing)**          | Mode 6. The drive finds a reference position using a configured method and sensor.                                      |

## Motors and drives

| Term                 | Meaning                                                                                                    |
| -------------------- | ---------------------------------------------------------------------------------------------------------- |
| **BLM**              | NiMotion's integrated BLDC servo series: motor, drive and encoder in one housing.                         |
| **BLM42 / BLM57**    | Frame sizes, 42 mm and 57 mm. BLM4203B and BLM4205B are BLM42; BLM5709B is BLM57.                         |
| **I2t**              | Thermal protection that limits how long the drive can run above rated current.                             |
| **CNET**             | NiMotion's name for the motor's CAN connector.                                                             |

## Other

| Term | Meaning |
| --- | --- |
| **PTU** | Pan-tilt unit: a camera mount driven by two NiMotion motors; the console this firmware came from was built to debug its motors. |
