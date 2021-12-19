// tcd - a hybrid transcoder using DVSI hardware and Codec2 software
// Copyright © 2021 Thomas A. Early N7TAE
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <unistd.h>
#include <iostream>

#include "Controller.h"

int main()
{
	CController controller;
	if (controller.Start())
		return EXIT_FAILURE;

	std::cout << "Hybrid Transcoder Version #211219 Successfully started" << std::endl;

	pause();

	controller.Stop();

	return EXIT_SUCCESS;
}
