import * as svgControl from './svgControl.js';
import * as client from './client.js';

// Setup is an overview of independent steps (hanger distance, belt position, pen,
// drawing). Each step opens its own screen and returns to the overview; the
// firmware decides what is allowed from its current state.

let currentState = null;
let currentWorker = null;
let uploadConvertedCommands = null;
let uploadPreviewDataUrl = null;
let penCal = {up: 90, down: 60, min: 0, max: 180};
let previewKey = null;      // which saved drawing the overview thumbnail shows
let movePollTimer = null;

window.onload = function () {
    init();
};

// ---------- navigation ----------

function showSlide(id) {
    $(".muralSlide").hide();
    $("#" + id).show();
}

function fetchState() {
    return $.getJSON("/getState").done(function (state) {
        currentState = state;
    });
}

function goOverview() {
    showSlide("loadingSlide");
    fetchState()
        .done(renderOverview)
        .fail(function () {
            alert("Couldn't reach Mural");
        });
}

function postForState(url, data, actionName) {
    showSlide("loadingSlide");
    return $.post(url, data || {})
        .done(function (state) {
            currentState = state;
            renderOverview(state);
        })
        .fail(function (xhr) {
            alert(`${actionName} failed: ${xhr.responseText || "no response"}`);
            goOverview();
        });
}

// Poll while a positioned move (extend to home / test move) runs, then call done().
function waitForMoveToFinish(done) {
    clearTimeout(movePollTimer);
    const poll = function () {
        fetchState()
            .done(function (state) {
                if (state.homing || state.moving) {
                    movePollTimer = setTimeout(poll, 1000);
                } else {
                    done(state);
                }
            })
            .fail(function () {
                movePollTimer = setTimeout(poll, 2000);
            });
    };
    movePollTimer = setTimeout(poll, 1000);
}

// ---------- overview ----------

function formatMm(mm) {
    return mm >= 1000 ? `${(mm / 1000).toFixed(1)} m` : `${Math.round(mm)} mm`;
}

function setStep(stepId, status, detail) {
    const icons = {
        done: "bi-check-circle-fill text-success",
        todo: "bi-circle text-secondary",
        warn: "bi-exclamation-circle-fill text-warning",
    };
    $(`#${stepId} .stepIcon`).attr("class", `stepIcon ${icons[status]}`);
    $(`#${stepId} .stepDetail`).text(detail);
}

function renderOverview(state) {
    currentState = state;
    if (state.drawingActive) {
        // A drawing is running (or paused) - show its status instead.
        showDrawing(state);
        return;
    }
    const ready = state.ready;
    const moving = state.homing || state.moving;

    const last = state.drawing;
    if (last && last.lastOutcome === "finished") {
        $("#lastDrawingNote").text(`Last drawing finished (${formatDuration(last.elapsedMs)} of drawing).`).show();
    } else if (last && last.lastOutcome === "stopped") {
        $("#lastDrawingNote").text(`Last drawing was stopped at ${last.progress}%. Mural is where it stopped; use "Test move to home" to park it.`).show();
    } else {
        $("#lastDrawingNote").hide();
    }

    // Hanger distance
    setStep("stepDistance", ready.distance ? "done" : "todo",
        ready.distance ? `${state.topDistance} mm apart` : "Not set");

    // Belt position
    const pos = state.position;
    const saved = state.savedPosition;
    let positionDetail;
    let positionStatus;
    if (pos.known) {
        positionStatus = pos.restored && !pos.testMoveDone ? "warn" : "done";
        positionDetail = pos.restored
            ? `Restored from last session${pos.testMoveDone ? ", test move done" : " (unverified)"}`
            : "Homed";
    } else if (saved.valid) {
        positionStatus = "todo";
        positionDetail = `Unknown. A saved position is available (hangers ${saved.topDistance} mm, pen at x=${saved.x.toFixed(0)} y=${saved.y.toFixed(0)} mm).`;
    } else {
        positionStatus = "todo";
        positionDetail = "Unknown, needs re-homing" + (saved.reason ? ` (${saved.reason})` : "");
    }
    setStep("stepPosition", positionStatus, positionDetail);
    $("#resumeSession").toggle(!pos.known && saved.valid).prop("disabled", moving);
    $("#resumeWarning").toggle((!pos.known && saved.valid) || (pos.restored && !pos.testMoveDone));
    $("#testMove").toggle(pos.known).prop("disabled", moving);
    $("#rehome").prop("disabled", moving || !ready.distance)
        .text(pos.known ? "Re-home" : "Home belts");

    // Pen
    setStep("stepPen", ready.pen ? "done" : "todo",
        ready.pen ? `Up ${state.pen.up}°, down ${state.pen.down}°` : "Not calibrated");
    $("#editPen").text(ready.pen ? "Edit" : "Calibrate");

    // Drawing
    const drawing = state.drawingFile;
    let drawingDetail;
    if (drawing.available && drawing.compatible) {
        drawingDetail = `${drawing.name || "drawing"}, ${formatMm(drawing.distance)} of travel`;
    } else if (drawing.available) {
        drawingDetail = `${drawing.name || "drawing"} was made for hangers ${drawing.topDistance} mm apart; choose it again for ${state.topDistance} mm`;
    } else {
        drawingDetail = "None loaded";
    }
    setStep("stepDrawing", ready.drawing ? "done" : (drawing.available ? "warn" : "todo"), drawingDetail);
    $("#chooseDrawing").prop("disabled", !ready.distance).text(ready.drawing ? "Change" : "Choose");
    renderOverviewPreview(drawing);

    // Start
    const missing = [];
    if (!ready.distance) missing.push("hanger distance");
    if (!ready.position) missing.push("belt position");
    if (!ready.pen) missing.push("pen calibration");
    if (!ready.drawing) missing.push("drawing");
    $("#beginDrawing").prop("disabled", !ready.all || moving);
    $("#notReadyHint").text(missing.length ? `Still needed: ${missing.join(", ")}.` : "");
    $("#moveStatus").text(moving ? "Moving…" : "");

    showSlide("overviewSlide");
}

// ---------- live drawing status ----------

let drawingPollTimer = null;
let drawingPollFailures = 0;

function formatDuration(ms) {
    const s = Math.round(ms / 1000);
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${sec}s`;
    return `${sec}s`;
}

function showDrawing(state) {
    renderDrawing(state);
    if (!$("#drawingSlide").is(":visible")) {
        // Re-use the overview thumbnail if we have it, otherwise fetch it once.
        const src = $("#overviewPreview").attr("src");
        if (src) {
            $("#drawingPreview").attr("src", src).show();
        } else if (state.drawingFile.hasPreview) {
            $.get("/savedPreview").done(function (dataUrl) {
                if (dataUrl && dataUrl.startsWith("data:")) {
                    $("#drawingPreview").attr("src", dataUrl).show();
                }
            });
        }
        showSlide("drawingSlide");
    }
    scheduleDrawingPoll();
}

function scheduleDrawingPoll() {
    clearTimeout(drawingPollTimer);
    drawingPollTimer = setTimeout(function () {
        fetchState()
            .done(function (state) {
                drawingPollFailures = 0;
                $("#drawingConnection").text("");
                if (state.drawingActive) {
                    renderDrawing(state);
                    scheduleDrawingPoll();
                } else {
                    renderOverview(state); // finished or stopped
                }
            })
            .fail(function () {
                drawingPollFailures++;
                if (drawingPollFailures >= 2) {
                    $("#drawingConnection").text("Lost contact with Mural, retrying… (it may have rebooted)");
                }
                scheduleDrawingPoll();
            });
    }, 2000);
}

function renderDrawing(state) {
    const d = state.drawing;
    const labels = {
        drawing: ["Drawing", "bg-primary"],
        pausing: ["Pausing…", "bg-warning text-dark"],
        paused: ["Paused", "bg-warning text-dark"],
        resuming: ["Resuming…", "bg-info text-dark"],
        stopping: ["Stopping…", "bg-danger"],
    };
    const [label, cls] = labels[d.state] || [d.state, "bg-secondary"];
    $("#drawingStateBadge").attr("class", `badge fs-6 ${cls}`).text(label + (d.state === "paused" ? (d.penDown ? "" : ", pen up") : ""));

    $("#drawingProgressBar").css("width", `${d.progress}%`).text(`${d.progress}%`);

    let stats = `${formatMm(d.distanceDone)} of ${formatMm(d.totalDistance)} · ${formatDuration(d.elapsedMs)} drawing`;
    if (d.distanceDone > 0 && d.totalDistance > d.distanceDone && d.elapsedMs > 30000) {
        const remainingMs = d.elapsedMs * (d.totalDistance - d.distanceDone) / d.distanceDone;
        stats += ` · about ${formatDuration(remainingMs)} left`;
    }
    $("#drawingStats").text(stats);

    const busy = d.state === "pausing" || d.state === "resuming" || d.state === "stopping";
    $("#pauseDrawing").toggle(d.state !== "paused").prop("disabled", busy);
    $("#resumeDrawing").toggle(d.state === "paused").prop("disabled", busy);
    $("#stopDrawing").prop("disabled", d.state === "stopping");

}

function drawingAction(url, actionName) {
    $("#pauseDrawing,#resumeDrawing,#stopDrawing").prop("disabled", true);
    $.post(url, {})
        .done(function (state) {
            currentState = state;
            renderDrawing(state);
            scheduleDrawingPoll();
        })
        .fail(function (xhr) {
            alert(`${actionName} failed: ${xhr.responseText || "no response"}`);
            goOverview();
        });
}

function renderOverviewPreview(drawing) {
    const img = $("#overviewPreview");
    if (!drawing.available || !drawing.hasPreview) {
        img.hide().removeAttr("src");
        previewKey = null;
        return;
    }
    const key = `${drawing.name}:${drawing.sizeBytes}`;
    if (key === previewKey) {
        img.show();
        return;
    }
    previewKey = key;
    $.get("/savedPreview").done(function (dataUrl) {
        if (previewKey === key && dataUrl && dataUrl.startsWith("data:")) {
            img.attr("src", dataUrl).show();
        }
    });
}

// ---------- pen calibration helpers ----------

function clampAngle(value) {
    return Math.max(penCal.min, Math.min(penCal.max, value));
}

function renderPenCalibration() {
    $("#penDownValue").text(penCal.down);
    $("#penUpValue").text(penCal.up);
    $("#servoRange").attr({min: penCal.min, max: penCal.max}).val(penCal.down);
}

function stopJogging() {
    $("#leftMotorToggle,#rightMotorToggle").prop("checked", false);
    client.leftRetractUp();
    client.rightRetractUp();
}

// ---------- page wiring ----------

function init() {
    $(".backToOverview").click(goOverview);

    // Hanger distance
    $("#editDistance").click(function () {
        const state = currentState;
        $("#distanceInput").val(state.topDistance > 0 ? state.topDistance : "");
        const warnings = [];
        if (state.position.known) warnings.push("Changing it means re-homing the belts.");
        if (state.drawingFile.available) warnings.push("The current drawing would need to be chosen again.");
        $("#distanceWarning").text(warnings.join(" "));
        showSlide("distanceSlide");
    });

    $("#setDistance").click(function () {
        const value = parseInt($("#distanceInput").val());
        if (isNaN(value) || value <= 0) {
            alert("Enter the distance in millimetres");
            return;
        }
        postForState("/setTopDistance", {distance: value}, "Setting the hanger distance");
    });

    // Belt position
    $("#resumeSession").click(function () {
        postForState("/resumeSession", {}, "Resume");
    });

    $("#testMove").click(function () {
        $("#testMove,#rehome,#beginDrawing").prop("disabled", true);
        $("#moveStatus").text("Moving to home… check the pen ends up where you expect.");
        $.post("/goHome", {})
            .done(function () {
                waitForMoveToFinish(renderOverview);
            })
            .fail(function (xhr) {
                alert("Test move failed: " + (xhr.responseText || "no response"));
                goOverview();
            });
    });

    $("#rehome").click(function () {
        $("#leftMotorToggle,#rightMotorToggle").prop("checked", false);
        $("#extendToHome").prop("disabled", false);
        $("#extendingStatus").hide();
        $("#beltsBack,#leftMotorToggle,#rightMotorToggle").prop("disabled", false);
        showSlide("beltsSlide");
    });

    $("#leftMotorToggle").change(function () {
        if (this.checked) client.leftRetractDown(); else client.leftRetractUp();
    });

    $("#rightMotorToggle").change(function () {
        if (this.checked) client.rightRetractDown(); else client.rightRetractUp();
    });

    $("#beltsBack").click(stopJogging);

    $("#extendToHome").click(async function () {
        $("#leftMotorToggle,#rightMotorToggle").prop("checked", false);
        await client.leftRetractUp();
        await client.rightRetractUp();
        $("#extendToHome,#beltsBack,#leftMotorToggle,#rightMotorToggle").prop("disabled", true);
        $("#extendingStatus").show();
        // Give the jog stop a moment to complete before the positioned move.
        setTimeout(function () {
            $.post("/extendToHome", {})
                .done(function () {
                    waitForMoveToFinish(renderOverview);
                })
                .fail(function (xhr) {
                    alert("Extend to home failed: " + (xhr.responseText || "no response"));
                    $("#extendToHome,#beltsBack,#leftMotorToggle,#rightMotorToggle").prop("disabled", false);
                    $("#extendingStatus").hide();
                });
        }, 500);
    });

    // Pen calibration. Values are raw servo angles; lower = towards the wall.
    // The firmware ramps every move and clamps to the soft limits.
    $("#editPen").click(function () {
        const pen = currentState.pen;
        penCal = {
            up: pen.up,
            down: pen.calibrated ? pen.down : Math.max(pen.min, pen.up - 30),
            min: pen.min,
            max: pen.max,
        };
        renderPenCalibration();
        showSlide("penCalibrationSlide");
    });

    const sendServo = $.throttle(250, function (angle) {
        $.post("/setServo", {angle});
    });

    $("#servoRange").on('input', function () {
        penCal.down = clampAngle(parseInt($(this).val()));
        renderPenCalibration();
        sendServo(penCal.down);
    });

    $(".penDownStep").click(function () {
        penCal.down = clampAngle(penCal.down + parseInt($(this).data("step")));
        renderPenCalibration();
        sendServo(penCal.down);
    });

    $(".penUpStep").click(function () {
        penCal.up = clampAngle(penCal.up + parseInt($(this).data("step")));
        renderPenCalibration();
        sendServo(penCal.up);
    });

    $("#penTestUp").click(() => $.post("/setServo", {angle: penCal.up}));
    $("#penTestDown").click(() => $.post("/setServo", {angle: penCal.down}));

    $("#setPenDistance").click(function () {
        if (penCal.down >= penCal.up &&
            !confirm(`The down angle (${penCal.down}°) is not lower than the up angle (${penCal.up}°). Lower angles move the pen towards the wall. Save anyway?`)) {
            return;
        }
        postForState("/setPenDistance", {angle: penCal.down, up: penCal.up}, "Saving pen calibration");
    });

    $("#penBack").click(function () {
        // Put the pen back to the saved up position before leaving.
        $.post("/servo", {action: "up"});
        goOverview();
    });

    // Drawing selection
    $("#chooseDrawing").click(function () {
        showSlide("svgUploadSlide");
    });

    async function getUploadedSvgString() {
        const [file] = $("#uploadSvg")[0].files;
        if (file) {
            return await file.text();
        } else {
            return null;
        }
    }

    $("#uploadSvg").change(async function () {
        const svgString = await getUploadedSvgString();
        if (svgString) {
            svgControl.setSvgString(svgString, currentState);

            $(".svg-control").show();
            $("#preview").removeAttr("disabled");
        } else {
            $("#preview").attr("disabled", "disabled");
            $(".svg-control").hide();
            $("#infillDensity").val(0);
            $("#turdSize").val(2);
        }
    });

    let currentPreviewId = 0;
    let rendererFn = null;

    async function render_VectorRasterVector() {
        if (currentWorker) {
            console.log("Terminating previous worker");
            currentWorker.terminate();
        }
        currentPreviewId++;
        const thisPreviewId = currentPreviewId;

        const svgString = await getUploadedSvgString();
        if (!svgString) {
            throw new Error('No SVG string');
        }

        $("#progressBar").text("Rasterizing");
        const raster = await svgControl.getCurrentSvgImageData();

        const vectorizeRequest = {
            type: 'vectorize',
            raster,
            turdSize: getTurdSize(),
        };

        if (currentPreviewId == thisPreviewId) {
            currentWorker = new Worker(`./worker/worker.js?v=${Date.now()}`);

            currentWorker.onmessage = (e) => {
                if (e.data.type === 'status') {
                    $("#progressBar").text(e.data.payload);
                } else if (e.data.type === 'vectorizer') {
                    const vectorizedSvg = e.data.payload.svg;
                    const scale = svgControl.getRenderScale();
                    renderSvgInWorker(
                        currentWorker,
                        vectorizedSvg,
                        svgControl.getTargetWidth() * scale,
                        svgControl.getTargetHeight() * scale,
                    );
                }
                else if (e.data.type === 'log') {
                    console.log(`Worker: ${e.data.payload}`);
                }
            }

            currentWorker.postMessage(vectorizeRequest);
        }
    }

    async function render_PathTracing() {
        if (currentWorker) {
            console.log("Terminating previous worker");
            currentWorker.terminate();
        }
        currentPreviewId++;
        const thisPreviewId = currentPreviewId;

        const svgString = await getUploadedSvgString();
        if (!svgString) {
            throw new Error('No SVG string');
        }

        if (currentPreviewId == thisPreviewId) {
            currentWorker = new Worker(`./worker/worker.js?v=${Date.now()}`);
            currentWorker.onmessage = (e) => {
                if (e.data.type === 'status') {
                    $("#progressBar").text(e.data.payload);
                }
                else if (e.data.type === 'log') {
                    console.log(`Worker: ${e.data.payload}`);
                }
            }

            const renderSvg = svgControl.getRenderSvg();
            const renderSvgString = new XMLSerializer().serializeToString(renderSvg);
            renderSvgInWorker(currentWorker, renderSvgString, svgControl.getTargetWidth(), svgControl.getTargetHeight());
        }
    }

    function renderSvgInWorker(worker, svg, svgWidth, svgHeight) {
        const svgJson = svgControl.getSvgJson(svg);

        const renderRequest = {
            type: "renderSvg",
            svgJson,
            width: svgControl.getTargetWidth(),
            height: svgControl.getTargetHeight(),
            svgWidth,
            svgHeight,
            homeX: currentState.homeX,
            homeY: currentState.homeY,
            infillDensity: getInfillDensity(),
            flattenPaths: getFlattenPaths(),
        }

        worker.onmessage = (e) => {
            if (e.data.type === 'status') {
                $("#progressBar").text(e.data.payload);
            } else if (e.data.type === 'renderer') {
                console.log("Worker finished!");

                uploadConvertedCommands = e.data.payload.commands.join('\n');
                const resultSvgJson = e.data.payload.svgJson;
                const resultDataUrl = svgControl.convertJsonToDataURL(resultSvgJson, svgControl.getTargetWidth(), svgControl.getTargetHeight());
                uploadPreviewDataUrl = resultDataUrl;

                const totalDistanceM = +(e.data.payload.distance / 1000).toFixed(1);
                const drawDistanceM = +(e.data.payload.drawDistance / 1000).toFixed(1);

                deactivateProgressBar();
                $("#previewSvg").attr("src", resultDataUrl);
                $("#distances").text(`Total: ${totalDistanceM}m / Draw: ${drawDistanceM}m`);
                $(".svg-preview").show();
                $("#acceptSvg").removeAttr("disabled");
            }
        };

        worker.postMessage(renderRequest);
    }

    function activateProgressBar() {
        const bar = $("#progressBar");
        bar.addClass("progress-bar-striped");
        bar.addClass("progress-bar-animated");
        bar.removeClass("bg-success");
        bar.text("");
    }

    function deactivateProgressBar() {
        const bar = $("#progressBar");
        bar.removeClass("progress-bar-striped");
        bar.removeClass("progress-bar-animated");
        bar.addClass("bg-success");
        bar.text("Success");
    }

    $("#infillDensity,#turdSize,#flattenPathsCheckbox").on('input change', async function () {
        activateProgressBar();
        $("#acceptSvg").attr("disabled", "disabled");
        await rendererFn();
    });

    $("#preview").click(async function () {
        showSlide("chooseRendererSlide");
    });

    $("#pathTracing").click(async function () {
        $("label[for='turdSize'],#turdSize").hide();
        $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").show();

        showSlide("drawingPreviewSlide");
        rendererFn = render_PathTracing;
        await rendererFn();
    });

    $("#vectorRasterVector").click(async function () {
        $("#flattenPathsCheckbox").prop("checked", false);
        $("label[for='turdSize'],#turdSize").show();
        $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").hide();

        showSlide("drawingPreviewSlide");
        rendererFn = render_VectorRasterVector;
        await rendererFn();
    });

    $(".backToSvgSelect").click(function () {
        uploadConvertedCommands = null;

        activateProgressBar();
        $("#previewSvg").removeAttr("src");
        $(".svg-preview").hide();
        $("#acceptSvg").attr("disabled", "disabled");

        showSlide("svgUploadSlide");
    });

    $("#acceptSvg").click(function () {
        if (!uploadConvertedCommands) {
            throw new Error('Commands are empty');
        }
        $("#acceptSvg").attr("disabled", "disabled");

        const commandsBlob = new Blob([uploadConvertedCommands], {
            type: "text/plain"
        });

        $("#uploadProgressBar > .progress-bar, #verificationProgress > .progress-bar").css("width", "0%");
        showSlide("uploadProgress");

        const formData = new FormData();
        formData.append("commands", commandsBlob);

        const [sourceFile] = $("#uploadSvg")[0].files;
        const drawingName = sourceFile ? sourceFile.name : "drawing";

        // Store the preview on Mural first (best effort) so the drawing can be
        // recognised and re-used after a reboot.
        const previewData = new FormData();
        previewData.append("preview", new Blob([uploadPreviewDataUrl || ""], {type: "text/plain"}));
        $.ajax({
            url: "/uploadPreview",
            data: previewData,
            processData: false,
            contentType: false,
            type: 'POST',
        }).always(function () {
            uploadCommandsFile(formData, drawingName);
        });
    });

    function uploadCommandsFile(formData, drawingName) {
        $.ajax({
            url: "/uploadCommands?name=" + encodeURIComponent(drawingName),
            data: formData,
            processData: false,
            contentType: false,
            type: 'POST',
            success: function () {
                verifyUpload();
            },
            error: function (xhr) {
                alert('Upload to Mural failed: ' + (xhr.responseText || "no response"));
                goOverview();
            },
            xhr: function () {
                const xhr = new window.XMLHttpRequest();
                xhr.upload.addEventListener("progress", function (evt) {
                    if (evt.lengthComputable) {
                        const percentComplete = parseInt(evt.loaded / evt.total * 100);
                        $("#uploadProgressBar > .progress-bar").css("width", `${percentComplete}%`);
                    }
                }, false);
                return xhr;
            },
        });
    }

    // Start drawing
    $("#beginDrawing").click(function () {
        const webOff = currentState && !currentState.webWhileDrawing;
        const prompt = webOff
            ? "Start drawing? The web UI is set to turn off while drawing, so this page won't be reachable until it finishes."
            : "Start drawing?";
        if (!confirm(prompt)) {
            return;
        }
        $("#beginDrawing").prop("disabled", true);
        $.post("/run", {})
            .done(function (state) {
                if (state && state.webWhileDrawing) {
                    currentState = state;
                    showDrawing(state);
                } else {
                    showSlide("drawingBegan");
                }
            })
            .fail(function (xhr) {
                alert("Couldn't start drawing: " + (xhr.responseText || "no response"));
                goOverview();
            });
    });

    $("#pauseDrawing").click(() => drawingAction("/pauseDrawing", "Pause"));
    $("#resumeDrawing").click(() => drawingAction("/resumeDrawing", "Resume"));
    $("#stopDrawing").click(function () {
        if (confirm("Stop the drawing? It can't be continued afterwards; you'd have to start it again from the beginning.")) {
            drawingAction("/stopDrawing", "Stop");
        }
    });

    // Tools modal
    $("#leftMotorTool").on('input', function () {
        const dir = parseInt($("#leftMotorTool").val());
        if (dir <= -1) client.leftRetractDown();
        else if (dir >= 1) client.leftExtendDown();
        else client.leftRetractUp();
    });

    $("#rightMotorTool").on('input', function () {
        const dir = parseInt($("#rightMotorTool").val());
        if (dir <= -1) client.rightRetractDown();
        else if (dir >= 1) client.rightExtendDown();
        else client.rightRetractUp();
    });

    $("#parkServoTool").click(function () {
        // Previously drove to 0 deg, the far end of travel, which can stall the servo
        // against its slot. "Up" is the calibrated rest position.
        $.post("/servo", {action: "up"});
    });

    $("#estepsTool").click(function () {
        $.post("/estepsCalibration", {}).fail(function (xhr) {
            alert("E-steps calibration failed: " + (xhr.responseText || "no response"));
        });
    });

    $("#toolsModal")[0].addEventListener('hidden.bs.modal', function () {
        $("#leftMotorTool,#rightMotorTool").val(0);
        client.rightRetractUp();
        client.leftRetractUp();
        // Jogging invalidates the belt position - refresh the overview.
        if ($("#overviewSlide").is(":visible")) {
            setTimeout(goOverview, 500);
        }
    });

    // Collapsible log panel - polls /log only while open.
    let logTimer = null;
    function refreshMainLog() {
        $.get("/log").done(function (text) {
            const log = $("#mainLog")[0];
            const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 30;
            $("#mainLog").text(text || "(empty)");
            if (atBottom) log.scrollTop = log.scrollHeight;
        }).fail(function () {
            $("#mainLog").text("Can't reach Mural (rebooting, out of range, or drawing with the web UI turned off).");
        });
    }
    $("#toggleLog").click(function () {
        const log = $("#mainLog");
        if (log.is(":visible")) {
            log.hide();
            $(this).text("Show log");
            clearInterval(logTimer);
            logTimer = null;
        } else {
            log.show();
            $(this).text("Hide log");
            refreshMainLog();
            log[0].scrollTop = log[0].scrollHeight;
            logTimer = setInterval(refreshMainLog, 2000);
        }
    });

    svgControl.initSvgControl();

    // Initial load: if a move is still running (e.g. page refreshed mid-homing),
    // wait for it before showing the overview.
    showSlide("loadingSlide");
    fetchState()
        .done(function (state) {
            if (state.homing || state.moving) {
                waitForMoveToFinish(renderOverview);
            } else {
                renderOverview(state);
            }
        })
        .fail(function () {
            alert("Failed to retrieve state");
        });
}

function verifyUpload() {
    $.ajax({
        url: "/downloadCommands",
        processData: false,
        contentType: false,
        type: 'GET',
        success: function (data) {
            const receivedData = data.split('\n');
            const sentData = uploadConvertedCommands.split('\n');
            let ok = receivedData.length === sentData.length;
            for (let i = 0; ok && i < receivedData.length; i++) {
                ok = receivedData[i] === sentData[i];
            }
            if (!ok) {
                alert("Data verification failed - please upload the drawing again");
            }
            previewKey = null; // force the overview thumbnail to reload
            setTimeout(goOverview, 500);
        },
        error: function () {
            alert('Failed to read the drawing back from Mural for verification');
            goOverview();
        },
        xhr: function () {
            const xhr = new window.XMLHttpRequest();
            xhr.addEventListener("progress", function (evt) {
                if (evt.lengthComputable) {
                    const percentComplete = parseInt(evt.loaded / evt.total * 100);
                    $("#verificationProgress > .progress-bar").css("width", `${percentComplete}%`);
                }
            }, false);
            return xhr;
        },
    });
}

function getInfillDensity() {
    const density = parseInt($("#infillDensity").val());
    if ([0, 1, 2, 3, 4].includes(density)) {
        return density;
    } else {
        throw new Error('Invalid density');
    }
}

function getTurdSize() {
    return parseInt($("#turdSize").val());
}

function getFlattenPaths() {
    return $("#flattenPathsCheckbox").is(":checked");
}
