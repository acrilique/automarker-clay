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
