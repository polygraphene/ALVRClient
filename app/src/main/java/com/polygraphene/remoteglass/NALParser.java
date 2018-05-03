package com.polygraphene.remoteglass;

import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.nio.BufferOverflowException;
import java.nio.BufferUnderflowException;
import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.List;

public class NALParser {
    private static final String TAG = "NALParser";

    long parsedNALCounter = 0;

    private List<ByteBuffer> packetQueue = new LinkedList<>();
    private List<byte[]> NALList = new LinkedList<>();

    enum h264ByteFormatState {
        ZERO_0,
        ZERO_1,
        ZERO_2,
        ZERO_3,
        NAL
    }

    h264ByteFormatState state = h264ByteFormatState.ZERO_0;
    ByteArrayOutputStream NALBuffer = new ByteArrayOutputStream();

    public List<byte[]> getNALList() {
        return NALList;
    }

    private boolean findZero(ByteBuffer buffer) {
        try {
            while (true) {
                if (buffer.get() == 0x00) {
                    return true;
                }
            }
        } catch (BufferOverflowException e) {
            return false;
        }
    }

    private int findNALEnd(byte[] buf, int offset, int length) {
        for (int i = offset; i < offset + length; i++) {
            if (i + 3 < offset + length &&
                    buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x00 && buf[i + 3] == 0x01) {
                return i;
                //} else if (i + 2 < offset + length &&
                //        buf[i] == 0x00 && buf[i + 1] == 0x00 && buf[i + 2] == 0x01) {
                //    return i;
            }
        }
        return -1;
    }

    public void replaceNAL3To4(ByteBuffer dest, ByteBuffer src) {
        int state = 0;
        dest.put(src.get());
        dest.put(src.get());
        try {
            while (true) {
                byte c = src.get();
                if (state == 0 && c == 0) {
                    state = 1;
                    dest.put((byte) 0);
                } else if (state == 1 && c == 0) {
                    state = 2;
                    dest.put((byte) 0);
                } else if (state == 2 && c == 1) {
                    state = 0;
                    dest.put((byte) 0);
                    dest.put((byte) 1);
                } else {
                    dest.put(c);
                    state = 0;
                }
            }
        } catch (BufferUnderflowException e) {

        }
    }

    public boolean parseNAL(ByteBuffer buffer) {
        try {
            while (true) {
                if (state == h264ByteFormatState.ZERO_0) {
                    if (findZero(buffer)) {
                        state = h264ByteFormatState.ZERO_1;
                        NALBuffer.reset();
                        NALBuffer.write(0);
                    } else {
                        NALBuffer.reset();
                        return false;
                    }
                }
                if (state == h264ByteFormatState.ZERO_1) {
                    byte c = buffer.get();
                    if (c == 0x00) {
                        NALBuffer.write(c);
                        state = h264ByteFormatState.ZERO_2;
                    } else {
                        state = h264ByteFormatState.ZERO_0;

                        continue;
                    }
                }
                if (state == h264ByteFormatState.ZERO_2) {
                    byte c = buffer.get();
                    if (c == 0x00) {
                        NALBuffer.write(c);
                        state = h264ByteFormatState.ZERO_3;
                    } else if (c == 0x01) {
                        NALBuffer.reset();
                        Log.v(TAG, "None Frame NAL has appeared!!");
                        //throw new IllegalStateException();
                        state = h264ByteFormatState.ZERO_0;
                        //state = h264ByteFormatState.NAL;
                    } else {
                        state = h264ByteFormatState.ZERO_0;

                        continue;
                    }
                }
                if (state == h264ByteFormatState.ZERO_3) {
                    byte c = buffer.get();
                    if (c == 0x00) {
                        // more than 3 zeroes
                        //state = h264ByteFormatState.ZERO_3;
                    } else if (c == 0x01) {
                        NALBuffer.write(c);
                        state = h264ByteFormatState.NAL;
                    } else {
                        state = h264ByteFormatState.ZERO_0;

                        continue;
                    }
                }
                if (state == h264ByteFormatState.NAL) {
                    int end = findNALEnd(buffer.array(), buffer.position(), buffer.remaining());
                    if (end == -1) {
                        NALBuffer.write(buffer.array(), buffer.position(), buffer.remaining());
                        return false;
                    }

                    byte[] buf = new byte[end - buffer.position() + NALBuffer.size()];
                    System.arraycopy(NALBuffer.toByteArray(), 0, buf, 0, NALBuffer.size());
                    buffer.get(buf, NALBuffer.size(), end - buffer.position());
                    NALList.add(buf);

                    int NALType = buf[4] & 0x1F;

                    parsedNALCounter++;
                    Log.v(TAG, parsedNALCounter + " NAL " + NALType + " " + buf.length);

                    NALBuffer.reset();

                    state = h264ByteFormatState.ZERO_0;
                }
            }
        } catch (BufferUnderflowException e) {
            return false;
        }
    }

    ByteBuffer getPacket() throws InterruptedException {
        while (true) {
            synchronized (this) {
                if (packetQueue.size() > 0) {
                    ByteBuffer packet = packetQueue.get(0);
                    packetQueue.remove(0);
                    return packet;
                }
            }
            Thread.sleep(1);
        }
    }

    synchronized ByteBuffer peekPacket() {
        if (packetQueue.size() > 0) {
            ByteBuffer packet = packetQueue.get(0);
            packetQueue.remove(0);
            return packet;
        }
        return null;
    }

    synchronized void flushPacketQueue() {
        packetQueue.clear();
    }

    void flushNALQueue() {
        NALList.clear();
    }

    byte[] peekNextNAL() {
        while (true) {
            if (!NALList.isEmpty()) {
                return NALList.get(0);
            }
            ByteBuffer packet = peekPacket();
            if (packet == null) {
                return null;
            }

            parseNAL(packet);
        }
    }

    byte[] recvNextNAL() {
        byte[] buf = peekNextNAL();
        if (buf == null) {
            return null;
        }
        NALList.remove(0);
        return buf;
    }

    synchronized int queuePacket(ByteBuffer buffer) {
        if (packetQueue.size() < 200) {
            ByteBuffer queueBuffer = buffer;
            packetQueue.add(queueBuffer);
        } else {
            Log.e(TAG, "packetQueue too long!");
            packetQueue.clear();
        }
        return packetQueue.size();
    }

    synchronized int getQueueSize() {
        return packetQueue.size();
    }
}
