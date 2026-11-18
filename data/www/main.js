import * as svgControl from './svgControl.js';
import * as client from './client.js';

let currentState = null;

let currentWorker = null;

window.onload = function () {
    init();
};

let uploadConvertedCommands = null;
let uploadPreviewDataUrl = null;
let penCal = {up: 90, down: 60, min: 0, max: 180};

async function checkIfExtendedToHome(extendToHomeTime) {
    await new Promise(r => setTimeout(r, extendToHomeTime * 1000));

    const waitPeriod = 2000;
    let done = false;
    while (!done) {
        try {
            const state = await $.get("/getState");
            if (state.phase !== 'ExtendToHome') {
                adaptToState(state);
                done = true;
            } else {
                await new Promise(r => setTimeout(r, waitPeriod));
            }
        } catch (err) {
            alert("Failed to get current phase: " + err);
            location.reload();
        }
    }
}

function init() {
    function doneWithPhase(custom) {
        $(".muralSlide").hide();
        $("#loadingSlide").show();
        if (!custom) {
            custom = {
                url: "/doneWithPhase",
                data: {},
                commandName: "Done With Phase",
            };
        }

        $.post(custom.url, custom.data || {}, function(state) {
            adaptToState(state);
        }).fail(function(xhr) {
            const detail = xhr && xhr.responseText ? `: ${xhr.responseText}` : "";
            alert(`${custom.commandName} command failed${detail}`);
            location.reload();
        });
    }

    $("#beltsRetracted").click(async function() { 
        await client.leftRetractUp();
        await client.rightRetractUp();
        doneWithPhase();
    });

    $("#setDistance").click(function() {
        const inputValue = parseInt($("#distanceInput").val());
        if (isNaN(inputValue)) {
            throw new Error("input value is not a number");
        }

        doneWithPhase({
            url: "/setTopDistance",
            data: {distance: inputValue},
            commandName: "Set Top Distance",
        });
    });

    $("#leftMotorToggle").change(function() {
        if (this.checked) {
            client.leftRetractDown(); 
        } else {
            client.leftRetractUp();
        }
    });

    $("#rightMotorToggle").change(function() {
        if (this.checked) {
            client.rightRetractDown(); 
        } else {
            client.rightRetractUp();
        }
    });

    $("#extendToHome").click(function() {
        $(this).prop( "disabled", true);
        $("#extendingSpinner").css('visibility', 'visible');
        $.post("/extendToHome", {})
        .always(async function(res) {
            const extendToHomeTime = parseInt(res);
            await checkIfExtendedToHome(extendToHomeTime);
        });
    });
    
    // Pen calibration. Values are raw servo angles; lower = towards the wall.
    // The firmware ramps every move and clamps to the soft limits.
    const sendServo = $.throttle(250, function (angle) {
        $.post("/setServo", {angle});
    });

    $("#servoRange").on('input', function () {
        penCal.down = clampAngle(parseInt($(this).val()));
        renderPenCalibration();
        sendServo(penCal.down);
    });

    $(".penDownStep").click(function() {
        penCal.down = clampAngle(penCal.down + parseInt($(this).data("step")));
        renderPenCalibration();
        sendServo(penCal.down);
    });

    $(".penUpStep").click(function() {
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
        doneWithPhase({
            url: "/setPenDistance",
            data: {angle: penCal.down, up: penCal.up},
            commandName: "Set Pen Distance",
        });
    });

    $("#resumeSession").click(function() {
        doneWithPhase({
            url: "/resumeSession",
            commandName: "Resume session",
        });
    });

    $("#useSavedDrawing").click(function() {
        doneWithPhase({
            url: "/useSavedDrawing",
            commandName: "Use saved drawing",
        });
    });

    $(".gotoPhase").click(function() {
        doneWithPhase({
            url: "/gotoPhase",
            data: {phase: $(this).data("phase")},
            commandName: "Go back",
        });
    });

    $("#goHome").click(function() {
        const button = $(this);
        button.prop("disabled", true);
        $("#goHomeStatus").text("Moving...");
        $.post("/goHome", {})
            .done(function(seconds) {
                $("#goHomeStatus").text(`Moving to home, about ${seconds}s. Check the pen ends up where you expect.`);
                setTimeout(() => {
                    button.prop("disabled", false);
                    $("#goHomeStatus").text("Done. If the position looks wrong, use Re-home belts.");
                }, (parseInt(seconds) + 1) * 1000);
            })
            .fail(function(xhr) {
                button.prop("disabled", false);
                $("#goHomeStatus").text("Failed: " + xhr.responseText);
            });
    });

    async function getUploadedSvgString() {
        const [file] = $("#uploadSvg")[0].files;
        if (file) {
            return await file.text();
        } else {
            return null;
        }
    }

    $("#uploadSvg").change(async function() {
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


    $("#infillDensity,#turdSize,#flattenPathsCheckbox").on('input change', async function() {
        activateProgressBar();
        $("#acceptSvg").attr("disabled", "disabled");
        await rendererFn();
    });

    $("#preview").click(async function() {
        $("#svgUploadSlide").hide();
        $("#chooseRendererSlide").show();
    });

    $("#pathTracing").click(async function() {
        $("label[for='turdSize'],#turdSize").hide();
        $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").show();

        $("#chooseRendererSlide").hide();
        $("#drawingPreviewSlide").show();
        rendererFn = render_PathTracing;
        await rendererFn();
    });

    $("#vectorRasterVector").click(async function() {
        $("#flattenPathsCheckbox").prop("checked", false);
        $("label[for='turdSize'],#turdSize").show();
        $("label[for='flattenPathsCheckbox'],#flattenPathsCheckbox").hide();

        $("#chooseRendererSlide").hide();
        $("#drawingPreviewSlide").show();
        rendererFn = render_VectorRasterVector;
        await rendererFn();
    });

    $(".backToSvgSelect").click(function() {
        uploadConvertedCommands = null;

        $(".loading").show();
        activateProgressBar();
        $("#previewSvg").removeAttr("src");
        $(".svg-preview").hide();
        $("#acceptSvg").attr("disabled", "disabled");

        $("#svgUploadSlide").show();
        $("#drawingPreviewSlide").hide();
        $("#chooseRendererSlide").hide();
    });
    
    $("#acceptSvg").click(function() {
        if (!uploadConvertedCommands) {
            throw new Error('Commands are empty');
        }
        $("#acceptSvg").attr("disabled", "disabled");

        const commandsBlob = new Blob([uploadConvertedCommands], {
            type: "text/plain"
        });

        $(".muralSlide").hide();
        $("#uploadProgress").show();

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
        }).always(function() {
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
            success: function(data) {
                verifyUpload(data);
            },
            error: function(err) {
                alert('Upload to Mural failed! ' + err);
                window.location.reload();
            },
            xhr: function () {
                var xhr = new window.XMLHttpRequest();

                xhr.upload.addEventListener("progress", function (evt) {
                    if (evt.lengthComputable) {
                        var percentComplete = evt.loaded / evt.total;
                        percentComplete = parseInt(percentComplete * 100);
                        $("#uploadProgress").attr("aria-valuemax", evt.total.toString());
                        $("#uploadProgress").attr("aria-valuenow", evt.loaded.toString());
                        $("#uploadProgress > .progress-bar").attr("style", `width: ${percentComplete}%`);
                    }
                }, false);

                return xhr;
            },
        });
    }

    $("#beginDrawing").click(function() {
        $.post("/run", {})
            .done(function() {
                $(".muralSlide").hide();
                $("#drawingBegan").show();
            })
            .fail(function(xhr) {
                alert("Couldn't start drawing: " + (xhr.responseText || "no response"));
            });
    });

    $("#reset").click(function() {
        doneWithPhase();
        location.reload();
    });

    $("#leftMotorTool").on('input', function() {
        const leftMotorDir = parseInt($("#leftMotorTool").val());
        if (leftMotorDir <= -1) {
            client.leftRetractDown(); 
        } else if (leftMotorDir >= 1) {
            client.leftExtendDown();
        } else {
            client.leftRetractUp();
        }
    });

    $("#rightMotorTool").on('input', function() {
        const rightMotorDir = parseInt($("#rightMotorTool").val());
        if (rightMotorDir <= -1) {
            client.rightRetractDown(); 
        } else if (rightMotorDir >= 1) {
            client.rightExtendDown();
        } else {
            client.rightRetractUp();
        }
    });

    $("#parkServoTool").click(function() {
        // Previously drove to 0 deg, the far end of travel, which can stall the servo
        // against its slot. "Up" is the calibrated rest position.
        $.post("/servo", {action: "up"});
    });

    $("#estepsTool").click(function() {
        $.post("/estepsCalibration", {});
    });

    const toolsModal = $("#toolsModal")[0];

    toolsModal.addEventListener('hidden.bs.modal', function (event) {
        client.rightRetractUp();
        client.leftRetractUp();
    });

    // Collapsible log panel - polls /log only while open.
    let logTimer = null;
    function refreshMainLog() {
        $.get("/log").done(function(text) {
            const log = $("#mainLog")[0];
            const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 30;
            $("#mainLog").text(text || "(empty)");
            if (atBottom) log.scrollTop = log.scrollHeight;
        }).fail(function() {
            $("#mainLog").text("Can't reach Mural (rebooting, or drawing - the web UI is off while drawing).");
        });
    }
    $("#toggleLog").click(function() {
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

    $("#loadingSlide").show();

    // adaptToState({
    //     phase: "BeginDrawing",
    //     topDistance: 1727,
    //     safeWidth: 1000,
    //     homeX: 0,
    //     homeY: 0,
    // });

    $.get("/getState", function(data) {
        adaptToState(data);
    }).fail(function() {
        alert("Failed to retrieve state");
    });
}

function verifyUpload(state) {
    $.ajax({
            url: "/downloadCommands",
            processData: false,
            contentType: false,
            type: 'GET',
            success: function(data) {
                const receivedData = data.split('\n');
                const sentData = uploadConvertedCommands.split('\n');
                if (receivedData.length !== sentData.length) {
                    alert("Data verification failed");
                    window.location.reload();
                    return;
                }
                for (let i = 0; i < receivedData.length; i++) {
                    if (receivedData[i] !== sentData[i]) {
                        alert("Data verification failed");
                        window.location.reload();
                        return;
                    }
                }
                setTimeout(function() {
                    adaptToState(state);
                }, 1000);
            },
            error: function(err) {
                alert('Failed to download commands from Mural! ' + err);
                window.location.reload();
            },
            xhr: function () {
                var xhr = new window.XMLHttpRequest();
                xhr.addEventListener("progress", function (evt) {
                    if (evt.lengthComputable) {
                        var percentComplete = evt.loaded / evt.total;
                        percentComplete = parseInt(percentComplete * 100);
                        $("#verificationProgress").attr("aria-valuemax", evt.total.toString());
                        $("#verificationProgress").attr("aria-valuenow", evt.loaded.toString());
                        $("#verificationProgress > .progress-bar").attr("style", `width: ${percentComplete}%`);
                    }
                }, false);

                return xhr;
            },
        });
    
}

function adaptToState(state) {
    $(".muralSlide").hide();
    currentState = state;
    switch(state.phase) {
        case "RetractBelts":
            $("#retractBeltsSlide").show();
            break;
        case "SetTopDistance":
            showSetTopDistance(state);
            break;
        case "ExtendToHome":
            $("#extendToHomeSlide").show();
            if (state.moving || state.startedHoming) {
                $("#extendToHome").prop( "disabled", true);
                $("#extendingSpinner").css('visibility', 'visible');
                checkIfExtendedToHome();
            }
            break;
        case "PenCalibration":
            // No automatic servo move here any more - the pen is already up.
            penCal = {
                up: state.pen.up,
                down: state.pen.calibrated ? state.pen.down : Math.max(state.pen.min, state.pen.up - 30),
                min: state.pen.min,
                max: state.pen.max,
            };
            renderPenCalibration();
            $("#penCalibrationSlide").show();
            break;
        case "SvgSelect":
            showSvgSelect(state);
            break;
        case "BeginDrawing":
            showBeginDrawing(state);
            break;
        default:
            alert("Unrecognized phase");
    }
}

function clampAngle(value) {
    return Math.max(penCal.min, Math.min(penCal.max, value));
}

function renderPenCalibration() {
    $("#penDownValue").text(penCal.down);
    $("#penUpValue").text(penCal.up);
    $("#servoRange").attr({min: penCal.min, max: penCal.max}).val(penCal.down);
}

function formatMm(mm) {
    return mm >= 1000 ? `${(mm / 1000).toFixed(1)} m` : `${Math.round(mm)} mm`;
}

function showSetTopDistance(state) {
    if (state.savedTopDistance > 0 && !$("#distanceInput").val()) {
        $("#distanceInput").val(state.savedTopDistance);
    }

    const pos = state.savedPosition;
    if (pos && pos.valid) {
        const penText = state.pen.calibrated
            ? `pen up ${state.pen.up}° / down ${state.pen.down}°`
            : "pen not calibrated yet";
        $("#resumeDetails").text(
            `Hangers ${pos.topDistance} mm apart, last position x=${pos.x.toFixed(0)} y=${pos.y.toFixed(0)} mm, ${penText}.`);
        $("#resumeCard").show();
        $("#noResumeNote").hide();
    } else {
        $("#resumeCard").hide();
        if (pos && pos.reason) {
            $("#noResumeNote").text(`Resume isn't available: ${pos.reason}, so the belts need re-homing.`).show();
        } else {
            $("#noResumeNote").hide();
        }
    }
    $("#distanceBetweenAnchorsSlide").show();
}

function showSvgSelect(state) {
    const saved = state.savedDrawing;
    if (saved && saved.available) {
        let details = `${saved.name || "drawing"} · ${formatMm(saved.distance)} of travel`;
        if (!saved.compatible) {
            details += ` · made for hangers ${saved.topDistance} mm apart (now ${state.topDistance} mm), so it can't be re-used`;
        }
        $("#savedDrawingDetails").text(details);
        $("#useSavedDrawing").prop("disabled", !saved.compatible);

        const img = $("#savedDrawingPreview");
        img.hide().removeAttr("src");
        if (saved.hasPreview) {
            $.get("/savedPreview").done(function(dataUrl) {
                if (dataUrl && dataUrl.startsWith("data:")) {
                    img.attr("src", dataUrl).show();
                }
            });
        }
        $("#savedDrawingCard").show();
    } else {
        $("#savedDrawingCard").hide();
    }
    $("#svgUploadSlide").show();
}

function showBeginDrawing(state) {
    const summary = $("#readySummary").empty();
    const item = (label, value) => $("<li class='list-group-item d-flex justify-content-between'></li>")
        .append($("<span></span>").text(label))
        .append($("<span class='text-muted'></span>").text(value));
    summary.append(item("Hangers", `${state.topDistance} mm`));
    summary.append(item("Pen", `up ${state.pen.up}° / down ${state.pen.down}°`));
    if (state.savedDrawing && state.savedDrawing.available) {
        summary.append(item("Drawing", `${state.savedDrawing.name} (${formatMm(state.savedDrawing.distance)})`));
    }
    $("#goHomeStatus").text("");
    $("#beginDrawingSlide").show();
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