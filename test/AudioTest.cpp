#include <coco/debug.hpp>
#include "AudioTest.hpp"
#ifdef NATIVE
#include <string>
#include <iostream>
#endif
#include <cmath>


Coroutine out(Loop &loop, Buffer &buffer1, Buffer &buffer2) {
	Sample table[128];
	for (int i = 0; i < 128; ++i) {
		table[i] = SCALE * sin(i * 6.28318530718 / 128.0);
	}

	int j = 0;
	while (true) {
		co_await buffer1.untilReadyOrDisabled();
		auto data1 = buffer1.pointer<Sample>();
		for (int i = 0; i < SAMPLE_COUNT; ++i) {
			data1[i] = table[j & 127];
			++j;
		}
#ifdef NATIVE
		std::cout << "start 1" << std::endl;
#else
		//debug::toggleRed();
#endif
		buffer1.startWrite(SAMPLE_COUNT * sizeof(Sample));

		co_await buffer2.untilReadyOrDisabled();
		auto data2 = buffer2.pointer<Sample>();
		for (int i = 0; i < SAMPLE_COUNT; ++i) {
			data2[i] = table[j & 127];
			++j;
		}
#ifdef NATIVE
		std::cout << "start 2" << std::endl;
#else
		//debug::toggleGreen();
#endif
		buffer2.startWrite(SAMPLE_COUNT * sizeof(Sample));
	}
}


int main() {

	out(drivers.loop, drivers.buffer1, drivers.buffer2);

	drivers.loop.run();
}
