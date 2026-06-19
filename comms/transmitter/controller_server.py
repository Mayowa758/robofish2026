
import pygame
import serial


SERIAL_PORT="/dev/cu.usbmodem11101"

pygame.init()         
pygame.joystick.init()  

joy = pygame.joystick.Joystick(0)
joy.init()  

print(f"Joystick name: {joy.get_name()}")
print(f"Axes: {joy.get_numaxes()}")
joy = pygame.joystick.Joystick(0)

ser = serial.Serial(SERIAL_PORT, 9600, timeout=1)


while True:
    pygame.event.pump()
    x_axis = joy.get_axis(0)  # left stick X
    print(x_axis)
    ser.write(f"{x_axis}\n".encode())