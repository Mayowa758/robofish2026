import pygame
import time

pygame.init()
pygame.joystick.init()

if pygame.joystick.get_count() == 0:
    print("No controllers detected. Please connect your PS4 controller.")
    pygame.quit()
    exit()

controller = pygame.joystick.Joystick(0)
controller.init()

print(f"Connected: {controller.get_name()}")
print("Move the joysticks. Press Ctrl+C to exit.\n")

try:
    while True:
        pygame.event.pump()

        # Left joystick
        left_x = controller.get_axis(0)
        left_y = controller.get_axis(1)

        # Right joystick
        right_x = controller.get_axis(2)
        right_y = controller.get_axis(3)

        print(
            f"Left Joystick  X: {left_x:.3f}  Y: {left_y:.3f} | "
            f"Right Joystick X: {right_x:.3f}  Y: {right_y:.3f}",
            end="\r",
            flush=True
        )

        time.sleep(0.1)

except KeyboardInterrupt:
    print("\nExiting...")

finally:
    pygame.quit()