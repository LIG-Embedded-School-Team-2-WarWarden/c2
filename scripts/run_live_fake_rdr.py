"""Target editor connected to the manual-codec live FCS station; no Protobuf."""
from pathlib import Path
import sys
import tkinter as tk
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'fcs-live'))
from rdr_sim.gui import App

root = tk.Tk()
app = App(root)
root.title('FAKE RDR - live FCS test targets')
app.bind_ip.set('127.0.0.1')
app.bind_port.set('1001')
app.ecs_ip.set('127.0.0.1')
app.ecs_port.set('11000')
app.demo_button.configure(state='disabled', text='Live ECS running separately')
app.start_server()
app.native_text.set('Manual-codec C++ ECS / real FCS; edit targets here. Shared synthetic origin for this test.')
root.mainloop()
