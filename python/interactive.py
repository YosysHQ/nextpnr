# Pass this file to one of the Python script arguments (e.g. --pre-place interactive.py)
# to drop to a command-line interactive Python session in the middle of place and route 

import code
print("Press Ctrl+D to finish interactive session")
code.interact(local=locals())
