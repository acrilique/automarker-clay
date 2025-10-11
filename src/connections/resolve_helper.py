/**
 * Copyright (C) 2025 Lluc Simó Margalef
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

import sys
import DaVinciResolveScript as bmd

def add_markers(beats):
    resolve = bmd.scriptapp("Resolve")
    if not resolve:
        return

    projectManager = resolve.GetProjectManager()
    project = projectManager.GetCurrentProject()
    timeline = project.GetCurrentTimeline()

    if not timeline:
        return

    framerate = timeline.GetSetting("timelineFrameRate")

    for beat in beats:
        frame = int(float(beat) * framerate)
        timeline.AddMarker(frame, "Blue", "AutoMarker", "beat-related", 1)

def clear_all_markers():
    resolve = bmd.scriptapp("Resolve")
    if not resolve:
        return

    projectManager = resolve.GetProjectManager()
    project = projectManager.GetCurrentProject()
    timeline = project.GetCurrentTimeline()

    if not timeline:
        return

    timeline.DeleteMarkersByColor("Blue")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        command = sys.argv[1]
        if command == "add":
            beats = sys.argv[2:]
            add_markers(beats)
        elif command == "clear":
            clear_all_markers()
