// VDAI page: presets, commands and the automatic-daily-readout schedule.
// Ported from automatenunsinn.github.io/src/vdai.ts; talks to the /vdai/* CGI handlers.

// Build the 17-char VDAI option code from the checkboxes (mirrors buildVdaiCode in vdai.ts).
function buildVdaiCode() {
  var code = "";
  code += $("#vdaiDelete").checked ? "L" : "l";
  var eg = document.querySelector('input[name="eg"]:checked');
  code += (eg && eg.value != null) ? eg.value : " ";
  code += $("#vdaiStat").checked   ? "S" : " ";
  code += $("#vdaiLast20").checked ? "L" : " ";
  code += $("#vdaiCopy").checked   ? "K" : " ";
  code += $("#vdaiCheck").checked  ? "C" : " ";
  return code;
}

function updateVdaiCode() {
  $("#vdaiCode").value = buildVdaiCode();
}

function selectedPreset() {
  var el = document.querySelector('input[name="preset"]:checked');
  return el ? el.value : "vdai";
}

// Show only the cards relevant to the selected preset.
function updateVisibility() {
  var p = selectedPreset();
  $("#vdai-card").style.display   = (p == "vdai") ? "" : "none";
  $("#adpalt-card").style.display = (p == "adpalt") ? "" : "none";
  $("#adp-card").style.display    = (p == "adp") ? "" : "none";
}

function sendPreset() {
  updateVisibility();
  ajaxSpin("POST", "/vdai/preset?p=" + selectedPreset(), function() {
    showNotification("Preset applied");
  }, function(s, st) { showWarning("Preset failed: " + st); });
}

// Run a command endpoint that returns the captured output as plain text.
function runText(url, label) {
  $("#vdaiOut").value = "... " + label + " ...";
  ajaxSpin("GET", url, function(resp) {
    $("#vdaiOut").value = resp;
    showNotification(label + " done (" + resp.length + " bytes)");
    fetchRecords();
  }, function(s, st) {
    $("#vdaiOut").value = "";
    showWarning(label + " failed: " + st);
  });
}

function doReadout() {
  var url = "/vdai/readout?code=" + encodeURIComponent(buildVdaiCode());
  if ($("#readoutSave").checked) url += "&save=1";
  runText(url, "Readout");
}

function doEinsat() {
  runText("/vdai/einsat", "Einsatz");
}

function doCmd(name) {
  runText("/vdai/cmd?c=" + encodeURIComponent(name), name);
}

function sendTime(e) {
  e.preventDefault();
  var v = $("#adpaltTime").value; // yyyy-MM-ddTHH:mm
  if (!v) { showWarning("Pick a date/time first"); return; }
  var d = new Date(v);
  function p2(n) { return ("0" + n).slice(-2); }
  // HHMMDDMMYY, matching the web tool's **ZEIT* format
  var t = p2(d.getHours()) + p2(d.getMinutes()) + p2(d.getDate()) +
          p2(d.getMonth() + 1) + p2(d.getFullYear() % 100);
  ajaxSpin("GET", "/vdai/time?t=" + t, function() {
    showNotification("Zeit gesendet");
  }, function(s, st) { showWarning("Zeit senden failed: " + st); });
}

//===== daily schedule

function showDaily(data) {
  var f = $("#daily-form");
  f.enable.checked = !!data.enable;
  f.preset.value = data.preset;
  f.command.value = data.command;
  f.hour.value = data.hour;
  f.code.value = data.code;
}

function fetchDaily() {
  ajaxJson("GET", "/vdai/daily", showDaily, function() {
    window.setTimeout(fetchDaily, 1000);
  });
}

function saveDaily(e) {
  e.preventDefault();
  var f = $("#daily-form");
  var url = "/vdai/daily?enable=" + (f.enable.checked ? 1 : 0) +
            "&preset="  + f.preset.value +
            "&command=" + f.command.value +
            "&hour="    + (f.hour.value || 0) +
            "&code="    + encodeURIComponent(f.code.value);
  ajaxJsonSpin("POST", url, function(data) {
    showDaily(data);
    showNotification("Schedule saved");
  }, function(s, st) { showWarning("Save failed: " + st); });
}

//===== stored records

function fmtDate(rec) {
  if (rec.bootrel) return "boot+" + rec.ts + "s";
  if (!rec.date) return "(no date)";
  var s = "" + rec.date; // YYYYMMDD
  return s.slice(0, 4) + "-" + s.slice(4, 6) + "-" + s.slice(6, 8);
}

function showRecords(data) {
  $("#rec-spinner").setAttribute("hidden", "");
  $("#rec-info").innerHTML = data.records.length + " of " + data.slots + " slot(s) used";
  var html = "<tr><th>Date</th><th>Bytes</th><th></th></tr>";
  // newest first
  data.records.sort(function(a, b) { return b.seq - a.seq; });
  data.records.forEach(function(r) {
    html += "<tr><td>" + fmtDate(r) + "</td><td>" + r.len + "</td>" +
            "<td><a class=\"pure-button\" href=\"/vdai/records/get?i=" + r.i +
            "\">Download</a></td></tr>";
  });
  $("#rec-table").querySelector("tbody").innerHTML = html;
}

function fetchRecords() {
  ajaxJson("GET", "/vdai/records", showRecords, function() {
    window.setTimeout(fetchRecords, 2000);
  });
}

function clearRecords() {
  ajaxSpin("POST", "/vdai/records/clear", function() {
    showNotification("Records cleared");
    fetchRecords();
  }, function(s, st) { showWarning("Clear failed: " + st); });
}

//===== init

function vdaiInit() {
  // code builder
  ["vdaiDelete", "vdaiStat", "vdaiLast20", "vdaiCopy", "vdaiCheck"].forEach(function(id) {
    bnd($("#" + id), "change", updateVdaiCode);
  });
  domForEach(document.querySelectorAll('input[name="eg"]'), function(el) {
    bnd(el, "change", updateVdaiCode);
  });
  updateVdaiCode();

  // presets
  domForEach(document.querySelectorAll('input[name="preset"]'), function(el) {
    bnd(el, "change", sendPreset);
  });
  updateVisibility();

  // commands
  bnd($("#readoutBtn"), "click", function(e) { e.preventDefault(); doReadout(); });
  bnd($("#einsatBtn"),  "click", function(e) { e.preventDefault(); doEinsat(); });
  bnd($("#adpaltTimeBtn"), "click", sendTime);
  domForEach(document.querySelectorAll(".vdai-cmd"), function(el) {
    bnd(el, "click", function(e) { e.preventDefault(); doCmd(el.getAttribute("data-cmd")); });
  });

  // schedule + records
  bnd($("#daily-form"), "submit", saveDaily);
  bnd($("#rec-clear"), "click", function(e) { e.preventDefault(); clearRecords(); });
  fetchDaily();
  fetchRecords();
}
