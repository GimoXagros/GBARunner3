#pragma once

/// @brief Enum representing the type of color correction to use.
enum class GbaColorCorrection
{
    /// @brief No color correction is applied.
    None,

    /// @brief Color correction is applied that resembles the AGB-001 GBA screen.
    Agb001,

    /// @brief Color correction is applied that resembles the AGS-101 GBA SP screen.
    Ags101,

    /// @brief Color correction is applied that resembles the OXY-001 GB micro screen.
    Oxy001,

    /// @brief Color correction is applied that resembles the NTR-001 NDS Phat screen.
    NdsPhat,

    /// @brief Color correction is applied that resembles the NTR-001 NDS Phat screen.
    NdsLite,

    /// @brief Color correction is applied that resembles the PSP-1000 screen.
    PspPhat,

    /// @brief Color correction is applied that resembles the Nintendo Switch Classics GBA shadder.
    NsoIps,

    /// @brief Color correction is applied that resembles the Nintendo Switch Classics GBA shadder in OLED screen.
    NsoOled,

    /// @brief Color correction is applied that resembles the VisualBoy Advance shadder.
    Vba,

    /// @brief Color correction is applied that resembles the No$GBA Shadder.
    NoGBA,

    /// @brief Color correction is applied that resembles the mGBA Shadder.
    mGba
};
