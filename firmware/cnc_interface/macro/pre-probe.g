; Get current machine position
M5000 P0

; Make sure probe tool is selected
if { global.mosPTID != state.currentTool }
    T T{global.mosPTID}

G6550 I{global.mosTPID} X{global.mosMI[0]} Y{global.mosMI[1]}
G6550 I{global.mosTPID} Z{global.mosMI[2]}
