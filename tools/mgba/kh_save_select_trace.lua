-- Lightweight reference trace for the Kingdom Hearts: Chain of Memories
-- New Game/save-selection transition. This script does not contain or derive
-- ROM data and is only a known-good comparison trace, not a GBARunner3 test.
-- Load it from mGBA's Tools > Load script menu before entering New Game.

local IO_BASE = 0x04000000
local MAX_EVENTS = 256

local trace = {}
local lastSignature = nil
local traceBuffer = nil

local function r8(address)
    return emu:read8(address)
end

local function r16(address)
    return r8(address) | (r8(address + 1) << 8)
end

local function r32(address)
    return r16(address) | (r16(address + 2) << 16)
end

local function hex(value, width)
    return string.format("%0" .. width .. "X", value & ((1 << (width * 4)) - 1))
end

local function dmaSnapshot(channel)
    local base = IO_BASE + 0xB0 + channel * 12
    return {
        sad = r32(base),
        dad = r32(base + 4),
        count = r16(base + 8),
        control = r16(base + 10),
    }
end

local function timerSnapshot(channel)
    local base = IO_BASE + 0x100 + channel * 4
    return {
        count = r16(base),
        control = r16(base + 2),
    }
end

local function captureFrame()
    if not emu or emu:getGameCode() ~= "B8CJ" then
        return
    end

    local event = {
        frame = emu:currentFrame(),
        pc = emu:readRegister("pc") or 0,
        cpsr = emu:readRegister("cpsr") or 0,
        dispcnt = r16(IO_BASE),
        dispstat = r16(IO_BASE + 4),
        vcount = r16(IO_BASE + 6),
        ie = r16(IO_BASE + 0x200),
        irqFlags = r16(IO_BASE + 0x202),
        waitcnt = r16(IO_BASE + 0x204),
        ime = r16(IO_BASE + 0x208),
        dma = {},
        timer = {},
    }

    local signature = string.format("%04X:%04X:%04X:%04X:%04X",
        event.dispcnt, event.ie, event.irqFlags, r16(IO_BASE + 0xBA),
        r16(IO_BASE + 0xD6))

    for channel = 0, 3 do
        event.dma[channel] = dmaSnapshot(channel)
        event.timer[channel] = timerSnapshot(channel)
    end

    -- Preserve every frame around subsystem transitions while avoiding an
    -- instruction-by-instruction logger. A heartbeat is retained every 30
    -- frames so a polling loop remains visible.
    if signature == lastSignature and event.frame % 30 ~= 0 then
        return
    end
    lastSignature = signature

    trace[#trace + 1] = event
    if #trace > MAX_EVENTS then
        table.remove(trace, 1)
    end

    if not traceBuffer then
        traceBuffer = console:createBuffer("KH save-select reference trace")
    end
    traceBuffer:clear()
    for _, item in ipairs(trace) do
        traceBuffer:print(string.format(
            "F%06d PC=%08s CPSR=%08s DC=%04s DS=%04s VC=%03d IE=%04s IF=%04s IME=%04s WC=%04s\n",
            item.frame, hex(item.pc, 8), hex(item.cpsr, 8),
            hex(item.dispcnt, 4), hex(item.dispstat, 4), item.vcount,
            hex(item.ie, 4), hex(item.irqFlags, 4), hex(item.ime, 4), hex(item.waitcnt, 4)))
        for channel = 0, 3 do
            local dma = item.dma[channel]
            local timer = item.timer[channel]
            traceBuffer:print(string.format(
                " D%d %08s>%08s n=%04s c=%04s  T%d n=%04s c=%04s\n",
                channel, hex(dma.sad, 8), hex(dma.dad, 8),
                hex(dma.count, 4), hex(dma.control, 4), channel,
                hex(timer.count, 4), hex(timer.control, 4)))
        end
    end
end

callbacks:add("frame", captureFrame)
console:log("KH save-select reference trace armed for B8CJ (256-event ring buffer)")
