/*
 Copyright (c) 2025 Yassine Ahmed Ali

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

 #include "../include/glps_window_manager.h"
 #include <stdio.h>


 int main()
 {
  glps_WindowManager *wm = glps_wm_init();
  glps_wm_window_create(wm, "Test Window", 100, 100, 800, 600);
  while(!wm->should_close)
  {

  }
  return 0;

 }