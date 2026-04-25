//	Author: Aaron Tomalino
//	Template from github/MysteriousJ/Joystick-Input-Examples

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <linux/input.h>

struct Joystick
{
	bool connected;
	char buttonCount;
	short* buttonStates;
	char axisCount;
	short* axisStates;
	char name[128];
	int file;
};

Joystick openJoystick(const char* fileName)
{
	Joystick j = {0};
	int file = open(fileName, O_RDONLY | O_NONBLOCK);
	if (file != -1)
	{
		ioctl(file, JSIOCGBUTTONS, &j.buttonCount);
		j.buttonStates = (short*)calloc(j.buttonCount, sizeof(short));
		ioctl(file, JSIOCGAXES, &j.axisCount);
		j.axisStates = (short*)calloc(j.axisCount, sizeof(short));
		ioctl(file, JSIOCGNAME(sizeof(j.name)), j.name);
		j.file = file;
		j.connected = true;
	}
	return j;
}

void readJoystickInput(Joystick* joystick)
{
	while (1)
	{
		js_event event;
		int bytesRead = read(joystick->file, &event, sizeof(event));
		if (bytesRead == 0 || bytesRead == -1) return;

		if (event.type == JS_EVENT_BUTTON && event.number < joystick->buttonCount) {
			joystick->buttonStates[event.number] = event.value;
		}
		if (event.type == JS_EVENT_AXIS && event.number < joystick->axisCount) {
			joystick->axisStates[event.number] = event.value;
		}
	}
}

bool js_equals(const Joystick* js1, const Joystick* js2) {
	if (js1->connected != js2->connected) { return false; }

	if (js1->buttonCount != js2->buttonCount) { return false; }

	if (js1->axisCount != js2->axisCount) { return false; }

	if (js1->file != js2->file) { return false; }

	for (int i = 0; i < js1->buttonCount; i++) {
		if (js1->buttonStates[i] != js2->buttonStates[i]) { return false; }
	}
	for (int i = 0; i < js1->axisCount; i++) {
		if (js1->axisStates[i] != js2->axisStates[i]) { return false; }
	}
	return true;
}

Joystick jscopy(const Joystick* copyof) {
	Joystick copy = {0};
	copy.connected = copyof->connected;
	copy.buttonCount = copyof->buttonCount;
	copy.axisCount = copyof->axisCount;
	copy.file = copyof->file;

	copy.buttonStates = (short*)calloc(copy.buttonCount, sizeof(short));
	for (int i = 0; i < copy.buttonCount; i++) {
		copy.buttonStates[i] = copyof->buttonStates[i];
	}

	copy.axisStates = (short*)calloc(copy.axisCount, sizeof(short));
	for (int i = 0; i < copy.axisCount; i++) {
		copy.axisStates[i] = copyof->axisStates[i];
	}
	return copy;
}

int main()
{
	Joystick joystick = {0};

	char fileName[32];
	sprintf(fileName, "/dev/input/js%d", 0);
	joystick = openJoystick(fileName);

	while (1)
	{
		if (joystick.connected)
		{
			Joystick duplicate = jscopy(&joystick); 
			readJoystickInput(&joystick);

			//printf("%s - Axes: ", joystick.name);
			//for (char axisIndex=0; axisIndex<joystick.axisCount; ++axisIndex) {
			//	printf("%d:% 6d ", axisIndex, joystick.axisStates[axisIndex]);
			//}
			//printf("Buttons: ");
			//for (char buttonIndex=0; buttonIndex<joysticks.buttonCount; ++buttonIndex) {
			//	if (joystick.buttonStates[buttonIndex]) printf("%d ", buttonIndex);
			//}
			if (!js_equals(&joystick, &duplicate)) {
				//printf("Something Happened!");

				printf("%s - Axes: ", joystick.name);
				for (char axisIndex=0; axisIndex<joystick.axisCount; ++axisIndex) {
					printf("%d:% 6d ", axisIndex, joystick.axisStates[axisIndex]);
				}
				printf("Buttons: ");
				for (char buttonIndex=0; buttonIndex<joystick.buttonCount; ++buttonIndex) {
					if (joystick.buttonStates[buttonIndex]) printf("%d ", buttonIndex);
				}

				printf("\n");
			}
			free(duplicate.buttonStates);
			free(duplicate.axisStates);
		}
		fflush(stdout);
		usleep(16000);
	}
}
