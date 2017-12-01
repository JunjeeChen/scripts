import sys

# implement the Right direction
def crcBufferSlow(data, seed, poly):
    count = len(data)

    for i in range(count):
        crc_data = data[i]

        for index in range(8):
            if (((seed ^ crc_data) & 0x1) != 0):
                seed = ((seed >> 1) ^ poly)
            else:
                seed >>= 1

            crc_data >>= 1

    return (seed & 0xFFFF)