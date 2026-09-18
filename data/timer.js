
  // minify here: https://www.minifier.org/
   
  const maxCtrls = %MAX_CONTROLS%;
  var ctrlCount = %CTRL_COUNT%;
  const maxTimers = %MAX_TIMERS%; // timer0 is boost/adv so don't count it!
  var haveSNTP = %HAVE_SNTP%;
  var eCntr = 0;
  var errMsg = "";
  
  var sensors = {}; // input sensors

  // text version of enum in C code!!!
  const  OFF = "0"; 
  const  ON = "1";
  const  AUTO = "2";
  const  BOOST = "3";
  const  ADV = "4";


  // Get control, timer & sensor data
async function getControl(ctrlNum) {
  if (ctrlNum < ctrlCount) { // tab button clicked to load new control
    document.getElementById("C").value = ctrlNum; // update post hidden field

    // Remove active from all tablinks class btns
    Array.from(document.getElementsByClassName("tablinks")).forEach(tab => tab.classList.remove("active"));

    if (ctrlNum == -1) {     // select setTZ?
      document.getElementById("control").style.display = "none"; // hide ctrl tab
      makeSnsrSelect(document.getElementById('XS'), document.getElementById('XS').value);
      document.getElementById("setTZ").style.display = "block"; // show setTz
      document.getElementById("setTZbtn").classList.add("active");
    } else { // view control tab
      document.getElementById("setTZ").style.display = "none"; // hide setTz
      document.getElementById("control").style.display = "block"; // show ctrl tab
      document.getElementById("ctrlbtn" + ctrlNum).classList.add("active");
        
      const formData = new FormData();
      formData.append("C", ctrlNum); // add tab control id#
      const response = await fetch("getControl", {
        method: 'POST',
        body: formData,
      });
      if (response.ok) {
        /* get JSON control data from host and copy into HTML elements on page */
        var ctrl = await response.json();
        // extract json control parameter values and copy into html for control
        if (ctrl && ctrl.C == ctrlNum) {  // parsed OK and ID matches the ctrl we asked for!
        
          if (ctrl.SN) {
            document.getElementById("SN").value = ctrl.SN; // name
            setTabBtnName(ctrl.C, ctrl.SN);
          }
          if (ctrl.ST) document.getElementById("ST").value = (ctrl.ST - 1) || 1; // Fallback to global 1 timer
          if (ctrl.SP) document.getElementById("SP").value = ctrl.SP;
          if (ctrl.SL) Array.from(document.getElementById("SL").options).forEach(opt => opt.selected = (opt.value == ctrl.SL) ? 1 : 0); // activelo/hi
          if (ctrl.SX) Array.from(document.getElementById("SX").options).forEach(opt => opt.selected = (opt.value == ctrl.SX) ? 1 : 0); // exp ID

          if (ctrl.sensors) {
            sensors = ctrl.sensors; // get sensor list (BEFORE timers!)
            updateSnsrDelBtn();
          }

          document.getElementById('timers-container').innerHTML = ''; // Clear previous timer blocks
          // add timers values
          for (var t = 1; t < Number(document.getElementById("ST").value) + 1; t++) { // Use control-specific timer count
            addTimer(t);
            var timer = ctrl.timers[t];
            var id = "T" + t;
            document.getElementById(id + "O").value = timer.O.substring(0, 5); // HH:mm
            document.getElementById(id + "P").value = timer.P.substring(0, 9); // DDD:HH:MM

            document.getElementById(id + "W").value = timer.W;
            document.getElementById(id + "D").value = timer.D;
            document.getElementById(id + "M").value = timer.M;
            updateScheduleOverlay(t);
            
            document.getElementById(id + "T").value = timer.T;
            document.getElementById(id + "H").value = timer.H;
            Array.from(document.getElementById(id + "L").options).forEach(opt => opt.selected = (opt.value == timer.L) ? 1 : 0);
            Array.from(document.getElementById(id + "S").options).forEach(opt => opt.selected = (opt.value == timer.S) ? 1 : 0);
            toggleSensorFields(document.getElementById(id + "S"), t);
          }
          document.getElementById("I").className = (ctrl.I == 1) ? "on" : "off"; // set state of control indicator AFTER timers setup!
          setModeBtns(ctrl.A);        // settings buttons
          setTabBtn(ctrl.C, ctrl.I);  // set tab on/off state
        }
      } else {
        errMsg = "ERROR - unable to load control #" + ctrlNum;
      }
    }
  }
}

/*  Send POST req with updated data:

  Note that all POSTs include the 'C' control#.  The backend will load the specified 
  control before updating - this ensures that the backend remains sync'ed 
  to the web page!

  1.  If src.name == 'A'  then change control mode to src.value (0-4) 
  2.  If src.name = SC or SA then POST updated control data (name, pin, etc.)
      Backend add/delete timers until it matchs 'ST' if supplied
  3.  If src.name = XA then add/delete a sensor
  4.  If src.name = XC then update current sensor data.
  5.  If src.name = "TMR" then POST tmrformxxx adding the ctrl# :
      Backend updates the controls timer parms.  If any changes then updates the timer file.
*/
async function save(src) {
  if (src != undefined) {
    if (src.name == "A") {
      const formData = new FormData();
      setModeBtns(src.value); // update mode buttons immediately, getstatus will update tab indicators
      formData.append("A", src.value); // add mode button value to form
      formData.append("C", document.getElementById("C").value); // add current ctrl number
      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      errMsg = (response.ok ? "State saved " : "ERROR - can't save ") + document.getElementById("SN").value;
      
    } else if (src.id == "SC" || src.name == "SA") { // ctrl settings changed?
      // POST ctrl settings form
      const formData = new FormData(document.getElementById("ctrlSettingsForm"));
      formData.append("C", document.getElementById("C").value); // add currctrl# to form
      if (src.name == "SA") // add/del ctrls
        formData.append("SA", src.value); // value (A/D) in form
      else
        setTabBtnName(document.getElementById("C").value, document.getElementById("SN").value); // update tab ctrl name

      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      document.getElementById('CtrlSettingsModal').style.display = 'none'; // close modal
      if (!response.ok) {
        if (response.status == 422 && src.name == "SC") // invalid pin#
          errMsg = response.statusText;
        else
          errMsg = "ERROR - can't save ctrl";
      } else { 
        errMsg = "Ctrl changes saved";
          
        if (src.name == "SA") { // add/del ctrls btns - settings page (ctrl = -1)
          var tz = document.getElementById("setTZbtn");
          if (src.value == "A" && ctrlCount < maxCtrls) {
            var ctrlNum = ctrlCount; // id for new ctrl
            ctrlCount++;
            var btn = document.createElement("button");
            btn.className = "tablinks";
            btn.id = "ctrlbtn" + ctrlNum;
            btn.value = ctrlNum;
            btn.setAttribute("onclick", "getControl(this.value)");
            btn.innerHTML = '<span class="tab-icon"></span><span class="tab-text">Control ' + ctrlNum + '</span>';
            tz.insertAdjacentElement("beforebegin", btn); // add new tab before settings tab
          } else if (src.value == "D" ) {
            if (ctrlCount > 1) {
              ctrlCount--;
              if (tz.previousElementSibling != null) 
                tz.previousElementSibling.remove(); // remove tab that's before the settings tab
            }
          }
//          makeCtrlTabs(); // regen tabs (adds/removes ctrls)
//          getStatus(true); // reload all ctrl names
          updateDelBtn(); 

        } else {  // Dynamically add/remove timer blocks if num_timers has changed
          const newNum = Number(document.getElementById("ST").value);
          var num_timers = document.getElementsByClassName("timer-block").length; // doesn't include the timer template!
          if (newNum < 1) newNum = 1;
          if (newNum > maxTimers) newNum = maxTimers;
          while (num_timers < newNum) { // add new timers if reqd
            addTimer(num_timers + 1); // numbered from 1...
            num_timers++;
          }
          var parentDiv = document.getElementById("timers-container"); 
          while (num_timers > newNum) { // remove timers if reqd
            if (parentDiv.lastElementChild) parentDiv.removeChild(parentDiv.lastElementChild);
            num_timers--;
          }
        }
      } 

    } else if (src.name == "XA") { // add/del sensor
      const formData = new FormData();
      formData.append("XA", src.value); // add add/del value to form
      formData.append("XS", document.getElementById("XS").value); // add sensor ID to form for delete
      formData.append("C", document.getElementById("C").value); // add current ctrl number
      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      errMsg = (response.ok ? "Sensors updated" : "ERROR - can't save sensors");
      // # of sensors has changed - when user clicks a ctrl tab after this, the sensor lists get reloaded and the 
      // timers sensor lists get remade.
      if  (src.value == 'A') { // add a new sensor to end of list
        sensors.push({XT: "1", XP: "-1", XX:"-1", XM:"-1", XN:"Sensor " + sensors.length, XB:"0", XR:"0", XU:"0"})
      } else { 
        if (sensors.length) sensors.pop();
      }
      makeSnsrSelect(document.getElementById('XS'), document.getElementById('XS').value); // regen the settings page sensor list
      updateSnsrDelBtn();

    } else if (src.id == "XC") { // closed sensor modal after edit?
      // POST snsr settings form
      const formData = new FormData(document.getElementById("snsrSettingsForm"));
      formData.append("XC", 1); // add close modal element 
      let sensorID = document.getElementById("XS").value;
      formData.append("XS", sensorID); // add sensor ID for edit
      formData.append("C", document.getElementById("C").value); // add currctrl# to form
      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      document.getElementById('SnsrSettingsModal').style.display = 'none'; // close modal
      if (!response.ok)
        errMsg = "ERROR - can't save sensors";
      else {
        errMsg = "Sensors changes saved";
        // update the sensors from the page data
        sensors[sensorID].XN = document.getElementById("XN").value;
        sensors[sensorID].XT = document.getElementById("XT").value;
        sensors[sensorID].XX = document.getElementById("XX").value;
        sensors[sensorID].XM = document.getElementById("XM").value;
        sensors[sensorID].XP = document.getElementById("XP").value;
        sensors[sensorID].XB = document.getElementById("XB").value;
        sensors[sensorID].XR = document.getElementById("XR").value;
        sensors[sensorID].XU = document.getElementById("XU").value;
        // a sensor name may have changed but we are in the settings tab.  When user clicks a control tab the sensor dropdowns in each timer are regen'ed.
        makeSnsrSelect(document.getElementById('XS'), document.getElementById('XS').value); // regen the settings page sensor lisr AFTER updating sensors array!
      }

    } else if (src.name == "TMR") { // timer update
      const formData = new FormData(document.getElementById("tmrform" + src.value));
      formData.append("C", document.getElementById("C").value); // add tab control id#
      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      if (!response.ok) {
        if (response.status == 422) // invalid pin#
          errMsg = response.statusText;
        else
          errMsg = "ERROR - can't save timer";
      } else { 
        errMsg = "Timer changes saved";
      }

    } else if (src.id == "CN") { // controller name change?
      const formData = new FormData();
      formData.append("CN", src.value); // add add/del value to form
      formData.append("C", document.getElementById("C").value); // add current ctrl number
      const response = await fetch("setConfig", {
        method: 'POST',
        body: formData,
      });
      if (response.ok)
        document.getElementById("NAME").innerHTML = src.value; // update title
      else
        errMsg = "ERROR - can't save name";
    }
  }
}

function toggleSensorFields(selectElement, timerNum) {
  const timerBlock = document.getElementById('T' + timerNum);
  if (timerBlock) {
    const condition = timerBlock.querySelector('.timer-condition');
    const setpoint = timerBlock.querySelector('.timer-setpoint');
    const isNone = selectElement.value == -1;
    condition.classList.toggle('hidden', isNone);
    setpoint.classList.toggle('hidden', isNone);
  }
}

function openSnsrSettingsModal() {
  sel = document.getElementById('XS').value; // selected sensor #
  if (sel != -1) { // nothing available to edit
    // populate sensor modal with the current sensor values
    document.getElementById("XN").value = sensors[sel].XN;
    document.getElementById("XT").value = sensors[sel].XT;
    document.getElementById("XX").value = sensors[sel].XX;
    document.getElementById("XM").value = sensors[sel].XM;
    document.getElementById("XP").value = sensors[sel].XP;
    document.getElementById("XB").value = sensors[sel].XB;
    document.getElementById("XR").value = sensors[sel].XR;
    document.getElementById("XU").value = sensors[sel].XU;
    document.getElementById('SnsrSettingsModal').style.display = 'block'; // open edit modal
  }
}

function openCtrlSettingsModal() {
  document.getElementById('CtrlSettingsModal').style.display = 'block';
}

function setModeBtns(val) { // "0-4" for new mode state
  // setting - group of buttons
  var settings = document.getElementsByName("A");
  settings.forEach(function(setting) {
    if (setting.value == val) setting.classList.add("active");
    else setting.classList.remove("active");
    
    if (setting.value == ADV) { // must have timer1 on/off set for adv
      if (document.getElementById("T1O"))
        setting.disabled = (document.getElementById("T1O").value == "00:00" && document.getElementById("T1P").value == "000:00:00") ? true : false;
    } else if (setting.value == AUTO) { // must have an active timer to allow auto mode
      setting.disabled = true; // disable auto button unless a timer has a valid durations
      for (var t = 1; t < Number(document.getElementById("ST").value + 1); t++)
        if (document.getElementById("T" + t + "P"))
          if (document.getElementById("T" + t + "P").value != "000:00:00") {
            setting.disabled = false; // ensable auto if a timer have valid durations
            break; // found an active timer duration
          }
    }
  });
}

function makeCtrlTabs() {
  // construct tabs for all control available
  var tz = document.getElementById("setTZbtn");
  while (tz.previousElementSibling != null) tz.previousElementSibling.remove(); // remove all tabs before the settings tab
  for (var c = 0; c < ctrlCount; c++) {
    var btn = document.createElement("button");
    btn.className = "tablinks";
    btn.id = "ctrlbtn" + c;
    btn.value = c;
    btn.setAttribute("onclick", "getControl(this.value)");
    btn.innerHTML = '<span class="tab-icon"></span><span class="tab-text">Control ' + c + '</span>';
    tz.insertAdjacentElement("beforebegin", btn);
  }
}

function setTabBtn(c, state) { // state = true = on
  // Show/hide the 'on' icon in the tab
  const btn = document.getElementById("ctrlbtn" + c);
  if (btn) {
    const icon = btn.querySelector(".tab-icon");
    if (icon) {
      icon.classList.toggle("on", state);
    }
  }
}

function setTabBtnName(c, name) { // set tab on if on or boost & update name
  const textSpan = document.querySelector("#ctrlbtn" + c + " .tab-text");
  if (textSpan && name && name.length && name !== textSpan.innerText)
    textSpan.innerText = name;
}

function updateDelBtn() {
  // show/hide if ctrlCount < 2
  const delBtn = document.getElementById("SD");  
  if (ctrlCount < 2)
    delBtn.style.display = "none";
  else
    delBtn.style.display = "inline-block";
  // update text in del ctrl button from the last tab
  const textSpan = document.querySelector("#ctrlbtn" + (ctrlCount - 1) + " .tab-text");
  if (textSpan)
    delBtn.innerText = "Delete " + textSpan.innerText;
}

function updateSnsrDelBtn() {
  var delBtn = document.getElementById("XD");  
  if (sensors.length == undefined || sensors.length == 0)
    delBtn.style.display = "none";
  else {
    delBtn.style.display = "flex";
    delBtn.innerText = "Delete " + sensors[sensors.length - 1].XN; // last sensors name
  }
}
  
// update the on/off state of the controls in the tabs
// and the on/off indicator in the control IF it is the current control 
async function getStatus(all) {
  // show any error msg for 4 secs
  if (eCntr == 0) {
    if (errMsg) {
      document.getElementById("EE").innerHTML = errMsg; // update error message
      errMsg = "";
      eCntr =  2;
    } else
      document.getElementById("EE").innerHTML = ""; // delete any msg
  } else
    eCntr--;

  url = "getStatus" + (all ? "?btns" : "");
  response = await fetch(url); 
  
  if (response.ok) {
    var status = await response.json();

    if (status) { // parsed OK
      if (status.T !== undefined) document.getElementById("clock").innerText = status.T;
      if (status.D !== undefined) document.getElementById("date").innerText = status.D;
      // Update WiFi icon status
      if (status.W !== undefined) { // W represents haveSNTP
        const sntpIcon = document.getElementById("sntp-icon");
        if (sntpIcon) sntpIcon.classList.toggle("on", status.W);
      }

      if (status.C !== undefined) {
        for (var c = 0; c < ctrlCount; c++) {
          if (status.C[c] != undefined) {
            if (c == document.getElementById("C").value) { // update currently displayed ctrl
              // update any on/off indicator in the current control tab 
              document.getElementById("I").className = status.C[c].I == 1 ? "on" : "off";
              setModeBtns(status.C[c].A); // set control mode button states
              // update timer sensor readings
              if (status.V) {

                for (var t = 1; t < Number(document.getElementById("ST").value) + 1; t++) {
                  if (document.getElementById("T" + t)) { // got timer block
                    snsrIdx = document.getElementById("T" + t + "S").value; // current sensor idx
                    document.getElementById("T" + t + "V").innerHTML = snsrIdx == -1 ? "" : "(curr. " + status.V[snsrIdx] + ")";
                  }
                }
              }
            }
            setTabBtn(c, status.C[c].I); // update ctrltab color for on/off
            if (status.C[c].SN !== undefined)
              setTabBtnName(c, status.C[c].SN); // update name if supplied
          }
        }
      }
    }
  }
}

function toggleTooltip(id) {
  var tt = document.getElementById(id);
  tt.classList.toggle("show");
} 

// set all checked bits in any check list based on bits set in val matching the checkbox value (1-n) 
function setChecked(id, val) {
  document.getElementById(id).childNodes.forEach(child => {
    child.firstChild.checked = (Number(val) & (1 << child.firstChild.value)) ? true : false;});
}

var currentCalendarTimer = 0;

// open modal setting any inputs based on W/D/M bit values
function openCalendarModal(t) {
  currentCalendarTimer = t;
  document.getElementById('CalendarTitle').innerText = 'Timer ' + t + ' Schedule';
  
  // Populate modal with current timer's settings
  let id = "T" + t;
  setChecked('CalendarW', document.getElementById(id + "W").value);
  setChecked('CalendarD', document.getElementById(id + "D").value);
  setChecked('CalendarM', document.getElementById(id + "M").value);

  document.getElementById('CalendarModal').style.display = 'block';
}

// update D/W/M hidden inputs from the modal bits
function closeCalendarModal() {
  let t = currentCalendarTimer;
  let id = "T" + t;
  document.getElementById(id + "W").value = getChecked('CalendarW');
  document.getElementById(id + "D").value = getChecked('CalendarD');
  document.getElementById(id + "M").value = getChecked('CalendarM');
  document.getElementById('CalendarModal').style.display = 'none';
  updateScheduleOverlay(t); // update overlay button content on close
  document.getElementById("TMR" + t).submit(); // and save any new timer settings
}

function updateScheduleOverlay(t) {
  const weekDayNames = ["Sun", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat"];
  const monthNames = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

  const getSelectedText = (val, names, label) => {
      if (val == 0) return `All ${label}`; // No bits set means all are valid, so display nothing.
      let selected = [];
      for (let i = 1; i <= (names ? names.length : 31); i++) {
          if ((val & (1 << i)) !== 0) {
              selected.push(names ? names[i - 1] : i);
          }
      }
      // Check if all are selected (unlikely with UI, but good practice)
      if (selected.length === (names ? names.length : 31)) return `All ${label}`;
      
      return `${label}: ${selected.join(', ')}`;
  };

  let id = "T" + t;
  let weekdaysVal = document.getElementById(id + "W").value;
  document.getElementById("T" + t + "SchedDays").innerHTML = getSelectedText(weekdaysVal, weekDayNames, 'Days');

  let datesVal = document.getElementById(id + "D").value;
  document.getElementById("T" + t + "SchedDates").innerHTML = getSelectedText(datesVal, null, 'Dates');

  let monthsVal = document.getElementById(id + "M").value;
  document.getElementById("T" + t + "SchedMonths").innerHTML = getSelectedText(monthsVal, monthNames, 'Months');
}

function getChecked(id) {
    let val = 0;
    document.getElementById(id).childNodes.forEach(child => { if (child.firstChild.checked) val |= (1 << child.firstChild.value);});
    return val;
}

// limit pin# depending on expander and type
function setMaxPin(val, type, dest) {
  let pin = document.getElementById(dest);
  if (val == '-1') { // not expander
    pin.max = '63'; 
  } else {
    if (type == 1) // digital exp.
      pin.max='15';
    else
      pin.max='3';
  }
}

// dest = object to add list to, selects prevously selected item
function makeSnsrSelect(dest, value) {
  dest.innerHTML = ""; // clr any existing options
  for (var o = -1; o < sensors.length; o++) {
    var option = document.createElement("option");
    option.value = o; // start at -1 for none
    option.text = (o == -1) ? "none" : sensors[o].XN;
    option.selected = (o == value) ? true : false;
    dest.add(option);
  }
}

// Dynamically build a timer block
function addTimer(timerNum) {

  const timerClone = document.getElementById('timer-template').content.cloneNode(true);
  // Replace placeholders like {t} with the actual timer number
  const timerHtml = new XMLSerializer().serializeToString(timerClone).replace(/\{t\}/g, timerNum);
  document.getElementById('timers-container').insertAdjacentHTML('beforeend', timerHtml);

  // setup sensor selector for timer
  makeSnsrSelect(document.getElementById("T" + timerNum + "S"), -1); // select "none" as default
  toggleSensorFields(document.getElementById("T" + timerNum + "S"), timerNum); // hide setpoint & condition fields
  updateScheduleOverlay(timerNum); // display default timer days/week/month
}

// startup stuff
window.onload = function() { 
  const weekDayNames = ["Sun", "Mon","Tues","Wed","Thurs","Fri","Sat"];
  const monthNames = ["Jan", "Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"];

  makeCtrlTabs();

  // fix system name in title and edit field
  document.getElementById("NAME").innerHTML = "%NAME%"; // update title
  document.getElementById("CN").innerHTML = "%NAME%"; // update edit input

  // Timer blocks are now created dynamically in getControl()
  // Populate the single calendar modal
  const populateCalendarGrid = (containerId, count, names) => {
    let container = document.getElementById(containerId);
    for (let o = 1; o <= count; o++) {
      const input = document.createElement("input");
      input.value = o;
      input.type = "checkbox";
      input.name = containerId + "_chk";
      let label = document.createElement("label");
      label.appendChild(input);
      let span = document.createElement("span"); // ordinal(o) was here
      span.textContent = names ? names[o-1] : o;
      label.appendChild(span);
      container.appendChild(label);
    }
  };

  populateCalendarGrid("CalendarW", 7, weekDayNames);
  populateCalendarGrid("CalendarD", 31, null);
  populateCalendarGrid("CalendarM", 12, monthNames);

  getControl(0); // load control0 AFTER tabs created!
  getStatus(true); // get status of all ctrls incl. tab names
  updateDelBtn();
  updateSnsrDelBtn();

  setInterval(getStatus, 2000, false); // refresh timer status every 2 secs

}
