(function () {
    const POLL_MS = 1000;
    const LOG_POLL_MS = 3000;
    let settingsLoaded = false;
    let lastOk = 0;

    function row(label, value, cls) {
        const tr = $("<tr></tr>");
        tr.append($("<td></td>").text(label));
        const td = $("<td></td>").text(value);
        if (cls) td.addClass(cls);
        tr.append(td);
        return tr;
    }

    function fill(tableId, rows) {
        const body = $(tableId).empty();
        rows.forEach(r => body.append(row(...r)));
    }

    function duration(ms) {
        if (ms < 0) return "never";
        const s = Math.floor(ms / 1000);
        if (s < 60) return `${s}s`;
        const m = Math.floor(s / 60);
        if (m < 60) return `${m}m ${s % 60}s`;
        return `${Math.floor(m / 60)}h ${m % 60}m`;
    }

    function kb(bytes) {
        return `${(bytes / 1024).toFixed(0)} KB`;
    }

    function render(d) {
        const sys = d.system, servo = d.servo, motors = d.motors, saved = d.savedPosition, storage = d.storage;

        const brownouts = sys.resetHistory.filter(r => r.startsWith("BROWNOUT")).length;
        const crashes = sys.resetHistory.filter(r => r.startsWith("Crash") || r.includes("watchdog")).length;
        if (sys.resetReason.startsWith("BROWNOUT")) {
            $("#brownoutWarning").html("<b>The last reboot was a brownout.</b> The supply voltage dropped too low, most likely from a current spike (servo stalling or straining, or motors and servo starting together).").show();
        } else if (brownouts > 0 || crashes > 0) {
            $("#brownoutWarning").text(`Recent reboots include ${brownouts} brownout(s) and ${crashes} crash/watchdog reset(s) — see Reset history below.`).show();
        } else {
            $("#brownoutWarning").hide();
        }

        fill("#servoTable", [
            ["Commanded angle", `${servo.commandedAngle.toFixed(1)}° → target ${servo.targetAngle}°`],
            ["State", servo.moving ? "moving / settling" : (servo.attached ? "holding" : "released (no pulses)"), servo.moving ? "text-primary" : ""],
            ["Pulse width", servo.attached ? `${servo.pulseUs} µs` : "—"],
            ["Pen", servo.isDown ? "down" : "up / other"],
            ["Up / down angle", `${servo.upAngle}° / ${servo.downAngle < 0 ? "not calibrated" : servo.downAngle + "°"}`],
            ["Soft limits", `${servo.minAngle}° … ${servo.maxAngle}°`],
            ["Speed", `${servo.speedDegPerSec}°/s`],
            ["Last move", servo.lastMoveTo < 0 ? "—" : `${servo.lastMoveFrom < 0 ? "?" : servo.lastMoveFrom}° → ${servo.lastMoveTo}° in ${servo.lastMoveMs} ms`],
            ["Moves since boot", servo.moveCount],
            ["Test cycles left", Math.ceil(servo.cycleRemaining / 2)],
            ["Position known", servo.positionKnown ? "yes" : "no (waiting for first move after boot)"],
            ["Boot start angle", servo.bootStartAngle < 0 ? "unknown (first move was a jump)" : `${servo.bootStartAngle}° (last angle saved during setup)`],
        ]);

        fill("#systemTable", [
            ["Firmware", `${sys.firmware} (${sys.built})`],
            ["Uptime", duration(sys.uptimeMs)],
            ["Last reset", sys.resetReason, sys.resetReason.startsWith("BROWNOUT") || sys.resetReason.startsWith("Crash") ? "text-danger fw-bold" : ""],
            ["Reset history", sys.resetHistory.join(" ← ")],
            ["Boot count", sys.bootCount],
            ["Status", sys.drawing ? `${sys.phase}, ${sys.progress}%` : sys.phase],
            ["Free memory", `${kb(sys.freeHeap)} (lowest ${kb(sys.minFreeHeap)})`],
            ["WiFi signal", `${sys.wifiRssi} dBm${sys.wifiRssi < -75 ? " (weak)" : ""}`],
            ["IP", sys.ip],
            ["OLED", sys.oledPresent ? "detected" : "not fitted"],
        ]);

        fill("#motorTable", [
            ["Moving", motors.moving ? "yes" : "no"],
            ["Position known", motors.homed ? "yes" : "no (needs homing or resume)"],
            ["Hanger distance", motors.topDistance < 0 ? "not set" : `${motors.topDistance} mm`],
            ["Pen position", motors.homed ? `x=${motors.x.toFixed(1)} y=${motors.y.toFixed(1)} mm` : "—"],
            ["Steps L / R", `${motors.leftSteps} / ${motors.rightSteps}`],
            ["Saved position", saved.valid ? `x=${saved.x.toFixed(1)} y=${saved.y.toFixed(1)} (${saved.topDistance} mm hangers)` : `none${saved.invalidReason ? " — " + saved.invalidReason : ""}`],
            ["Saved steps L / R", saved.valid ? `${saved.leftSteps} / ${saved.rightSteps}` : "—"],
            ["Last saved", saved.ageMs < 0 ? "not this boot" : `${duration(saved.ageMs)} ago (${saved.saveCount} saves, last took ${(saved.lastSaveUs / 1000).toFixed(1)} ms)`],
            ["Saving", "during setup only, never while drawing"],
        ]);

        fill("#storageTable", [
            ["Flash filesystem", `${kb(storage.usedBytes)} used of ${kb(storage.totalBytes)}`],
            ["Saved drawing", storage.drawingValid ? `${storage.drawingName} (${kb(storage.drawingBytes)}, ${storage.drawingTopDistance} mm hangers)` : "none"],
        ]);

        if (!settingsLoaded) {
            $("#setUp").val(servo.upAngle);
            $("#setDown").val(servo.downAngle);
            $("#setMin").val(servo.minAngle);
            $("#setMax").val(servo.maxAngle);
            $("#setSpeed").val(servo.speedDegPerSec);
            $("#setRelease").prop("checked", servo.releaseWhenUp);
            $("#servoAngle").val(servo.targetAngle);
            settingsLoaded = true;
        }
    }

    function poll() {
        $.getJSON("/diagnostics")
            .done(function (d) {
                lastOk = Date.now();
                $("#connection").removeClass("alert-warning alert-secondary").addClass("alert-success").text("Connected");
                $("main").removeClass("stale");
                render(d);
                if (!settingTouched) {
                    $("#logDrawingMoves").prop("checked", d.system.logDrawingMoves);
                    $("#webWhileDrawing").prop("checked", d.system.webWhileDrawing);
                }
                $("#logDrawingMoves,#webWhileDrawing").prop("disabled", d.system.drawing);
            })
            .fail(function () {
                const since = lastOk ? ` (last update ${duration(Date.now() - lastOk)} ago)` : "";
                $("#connection").removeClass("alert-success alert-secondary").addClass("alert-warning")
                    .text("Can't reach Mural — it may be rebooting, drawing with the web UI turned off, or out of WiFi range" + since);
            })
            .always(function () {
                setTimeout(poll, POLL_MS);
            });
    }

    let settingTouched = false;
    $("#logDrawingMoves,#webWhileDrawing").change(function () {
        settingTouched = true;
        const data = {};
        data[this.id] = $(this).is(":checked") ? 1 : 0;
        $.post("/diagSettings", data)
            .fail(function (xhr) {
                alert("Couldn't change setting: " + (xhr.responseText || "no response"));
            })
            .always(function () {
                settingTouched = false; // resync with the device on the next poll
            });
    });

    function pollLog(force) {
        if (!force && !$("#logAuto").is(":checked")) {
            setTimeout(pollLog, LOG_POLL_MS);
            return;
        }
        $.get("/log")
            .done(function (text) {
                const log = $("#log")[0];
                const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 30;
                $("#log").text(text || "(empty)");
                if (atBottom) log.scrollTop = log.scrollHeight;
            })
            .always(function () {
                if (!force) setTimeout(pollLog, LOG_POLL_MS);
            });
    }

    function servo(action, value) {
        const data = {action};
        if (value !== undefined) data.value = value;
        return $.post("/servo", data).fail(function (xhr) {
            alert("Servo command failed: " + (xhr.responseText || "no response"));
        });
    }

    $(".servoAction").click(function () {
        servo($(this).data("action"), $(this).data("value"));
    });

    $("#servoGo").click(function () {
        servo("angle", parseInt($("#servoAngle").val()));
    });

    $("#cycleStart").click(function () {
        servo("cycle", parseInt($("#cycleCount").val()));
    });

    $("#saveServoSettings").click(function () {
        $("#servoSettingsStatus").text("Saving…");
        $.post("/servoSettings", {
            up: $("#setUp").val(),
            down: $("#setDown").val(),
            min: $("#setMin").val(),
            max: $("#setMax").val(),
            speed: $("#setSpeed").val(),
            releaseWhenUp: $("#setRelease").is(":checked") ? 1 : 0,
        }).done(function () {
            $("#servoSettingsStatus").text("Saved");
            settingsLoaded = false; // reload the (possibly clamped) values
        }).fail(function (xhr) {
            $("#servoSettingsStatus").text("Failed: " + (xhr.responseText || "no response"));
        });
    });

    $("#logCopy").click(function () {
        const text = $("#log").text();
        if (navigator.clipboard) {
            navigator.clipboard.writeText(text);
        } else {
            const ta = $("<textarea></textarea>").val(text).appendTo("body").select();
            document.execCommand("copy");
            ta.remove();
        }
    });

    poll();
    pollLog();
})();
