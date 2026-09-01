-- Lightweight reference trace for the Kingdom Hearts: Chain of Memories
-- New Game/save-selection transition. This script does not contain or derive
-- ROM data and is only a known-good comparison trace, not a GBARunner3 test.
-- Load it from mGBA's Tools > Load script menu, reach the Main Menu, then
-- press Select once to arm it before entering New Game.

local IO_BASE = 0x04000000
local MAX_EVENTS = 256

local trace = {}
local lastSignature = nil
local traceBuffer = nil
local armed = false
local previousKeys = 0
local armFrame = 0
local newGameInputFrame = nil
local firstHighRomPc = nil

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
    if not armed or not emu or emu:getGameCode() ~= "B8CJ" then
        return
    end

    local cpsr = emu:readRegister("cpsr") or 0
    local event = {
        frame = emu:currentFrame(),
        pc = emu:readRegister("pc") or 0,
        lr = emu:readRegister("lr") or 0,
        cpsr = cpsr,
        state = (cpsr & 0x20) ~= 0 and "THUMB" or "ARM",
        keys = emu:getKeys() or 0,
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

    if newGameInputFrame and not firstHighRomPc and
            event.pc >= 0x08200000 and event.pc < 0x0E000000 then
        firstHighRomPc = event.pc
        console:log(string.format(
            "KH reference checkpoint: first frame-observed high-ROM PC after New Game input = %08X at frame %d",
            firstHighRomPc, event.frame))
    end

    local signature = string.format("%08X:%08X:%04X:%04X:%04X:%04X:%04X",
        event.pc, event.lr,
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
            "F%06d +%04d PC=%08s LR=%08s %s CPSR=%08s KEY=%03s DC=%04s DS=%04s VC=%03d IE=%04s IF=%04s IME=%04s WC=%04s\n",
            item.frame, item.frame - armFrame, hex(item.pc, 8), hex(item.lr, 8),
            item.state, hex(item.cpsr, 8), hex(item.keys, 3),
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

local function observeKeys()
    if not emu or emu:getGameCode() ~= "B8CJ" then
        return
    end

    local keys = emu:getKeys() or 0
    local pressed = keys & ~previousKeys
    previousKeys = keys

    if not armed and (pressed & (1 << 2)) ~= 0 then -- Select
        armed = true
        armFrame = emu:currentFrame()
        trace = {}
        lastSignature = nil
        newGameInputFrame = nil
        firstHighRomPc = nil
        console:log("KH reference trace armed at Main Menu; wait one second, then select New Game")
    elseif armed and not newGameInputFrame and (pressed & 1) ~= 0 then -- A
        newGameInputFrame = emu:currentFrame()
        console:log(string.format("KH reference checkpoint: first A press after arm at frame %d", newGameInputFrame))
    end
end

callbacks:add("frame", captureFrame)
callbacks:add("keysRead", observeKeys)
console:log("KH save-select reference trace loaded for B8CJ; press Select at Main Menu to arm")
