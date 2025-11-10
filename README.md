<img width="907" height="383" alt="image" src="https://github.com/user-attachments/assets/89749103-039c-4c93-800a-5c51b9b63116" />
The following shows the layout of the breadboard project. 

<img width="658" height="551" alt="image" src="https://github.com/user-attachments/assets/85d8c456-025d-4d9d-8b74-d8b1f4ba524a" />
This infrared sensor must be attached in order for the motion detection to go along with the project. 

How to setup:
1. Prepare a circuit on a breadboard that emulates a device for access control. The
device has a WiFi module for connecting to wireless devices. It also connects to a
servo motor and LEDs to indicate a wrong or correct authentication.
2. Implement the logic in the access control device that validates an access code for
granting access to the premises. The input of the access code is via a wireless
connection with the user’s mobile/portable device.
3. Connect an Infrared sensor and calibrate it to detect a person or object close by.
4. Update the circuit and logic to give access after an authentication of 2 steps
(credentials and sensor).
5. Connect the device to TTN using LoRaWAN protocol.
6. Implement the logic that handles the event of users entering their credentials and
log every attempt in a Web platform o database integrate with TTN of your
preference.


