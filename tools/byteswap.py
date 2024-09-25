import sys

with open(sys.argv[1], "rb") as in_file:
    input = in_file.read()

output = bytearray((len(input) + 3) & ~3)
for i in range(len(input)):
    output[i ^ 3] = input[i]

with open(sys.argv[2], "wb") as out_file:
    out_file.write(output)