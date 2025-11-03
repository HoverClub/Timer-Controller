
/*
// on a return key OR on save key click OR settings button click 

 if a setting button click then only include the setting
  1. submit the new setting using fetch GET
  2. update the tab on/off status if changed (i.e ON/BOOST or OFF button click)


 if a return or save btn click then:
  1. only submit timer settings & control name
  2. update control name in it's tab if changed
*/
  var currCtrl = %C%;
  const maxCtrls = %MAX_CTRLS%;
  const maxTimers = %MAX_TIMERS%;
  const maxStats = %MAX_STATS%;
  const statNames = [%STAT_NAMES%]; // stat descriptors
  const haveSNTP = %HAVE_SNTP%;
  var errMsg = "";
  var eCntr = 0;

// save control or timer values
// if val == "" then it's a timer save, otherwise val == setting
async function save(val) {
  var name = document.getElementById("N").value;
  var ctrl = document.getElementById("C").value;
  var url = "setControl?C=" + ctrl + "&"; // ctrl ID
  if (val) { // change control settings only
    url += "A=" + val;
    setSettings(val); // update buttons state
    setTabBtn(ctrl, val == "O" || val == "B"); // set tab on if on or boost
  } else { // save timer settings
    setTabBtnName(ctrl, name); // update name
    url += "N=" + encodeURIComponent(name) + "&";
    for (var t = 1; t < maxTimers; t++) {
        url += "T" + t + "O=" + document.getElementById("T" + t + "O").value + "&";
        url += "T" + t + "F=" + document.getElementById("T" + t + "F").value + "&";
        url += "T" + t + "L=" + document.getElementById("T" + t + "L").value + "&";
        url += "T" + t + "H=" + document.getElementById("T" + t + "H").value + "&";
        url += "T" + t + "T=" + document.getElementById("T" + t + "T").value + "&";
        url += "T" + t + "S=" + document.getElementById("T" + t + "S").value + "&";
    }
  }
  const response = await fetch(url);
  errMsg = response.ok ? "Changes saved for " + name : "ERROR - unable to save changes for " + name;
}

function setSettings(val) {
  // setting - group of buttons
  var settings = document.getElementsByName("A");
  settings.forEach(function(setting) {
    if (setting.value == val) {
      setting.className = "active";
    } else
      setting.className = "";
    if (setting.value == "V") { // must have timer1 on/off set for adv
      setting.disabled = (document.getElementById("T1O").value == document.getElementById("T1F").value) ? true : false;
    } else if (setting.value == "A") { // any timer on for auto
      var timeSet = false;
      for (var t = 1; t < maxTimers; t++)
        timeSet |= document.getElementById("T" + t + "O").value != document.getElementById("T" + t + "F").value;
      setting.disabled = timeSet ? false : true;
    }
  });
}

function setTabBtn(c, state, name) {
  // change color of ctrltab of any ctrl that is on
  var btn = document.getElementById("ctrlbtn" + c);
  btn.className = btn.className.replace(" on", ""); // remove any existing ON class
  if (state)
    btn.className += " on";
}

function setTabBtnName(c, name) { // set tab on if on or boost & update name
  var btn = document.getElementById("ctrlbtn" + c);
  // update name if it has changed
  if (name && name.length && name != btn.innerHTML)
    btn.innerHTML = name;
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

  if (all)
    response = await fetch("getAllStatus"); // inc. names!
  else
    response = await fetch("getStatus");
  
  if (response.ok) {
    var status = await response.json();

    if (status) { // parsed OK
      if (status.T !== undefined) {
        const t = new Date(Number(status.T) * 1000)
        .toISOString()
        .slice(11, 19);
        document.getElementById("clock").innerText = t; // adding time to the div
      }

      if (status.C !== undefined) {
        for (var c = 0; c < maxCtrls; c++) {
          if (c == currCtrl) {
            // update any on/off indicator in the current control tab 
            document.getElementById("I").className = status.C[c].I == 1 ? "on" : "off"
            setSettings(status.C[c].A); // and settings buttons
            // update timer sensor readings
            if (status.V) {
              for (var t = 1; t < maxTimers; t++)
                if (document.getElementById("T" + t + "S").value == -1)
                  document.getElementById("T" + t + "V").innerHTML = "";
                else
                  document.getElementById("T" + t + "V").innerHTML = "(curr " + status.V[t] + ")";
            }
          }
          setTabBtn(c, status.C[c].I); // update ctrltab color for on/off
          if (all && status.C[c].N !== undefined)
            setTabBtnName(c, status.C[c].N); // update name if supplied
        }
      }
    }
  }
}

function myFunction() {
  var tt = document.getElementById("tooltipdemo");
  tt.classList.toggle("show");
} 

function currentTime(secs) {

}

/* get JSON control data from host and copy into HTML elements on page
  {
    "id": 0,
    "time": "54292",
    "state": false,
    "name": "Control ",
    "setting": "F",
    "timers": [
      {
        "id": 2,
        "onTime": 0,
        "offTime": 0,
        "temp": 20,
        "lessthan": "L",
        "stat": -1
      },
      {
        "id": 3,
        "onTime": 0,
        "offTime": 0,
        "temp": 20,
        "lessthan": "L"
        "stat": -1
      }
    ]
  }   */
async function getControl(ctrlNum) {
  errMsg = ""; // clr errors
  if (ctrlNum < maxCtrls) {
    currCtrl = ctrlNum;
    document.getElementById("C").value = ctrlNum; // update post hidden field

    // Get all buttons with class="tablinks" and remove the class "active"
    tablinks = document.getElementsByClassName("tablinks");
    for (i = 0; i < tablinks.length; i++) {
      tablinks[i].className = tablinks[i].className.replace(" active", "");
    }

    if (ctrlNum == -1) {     // select setTZ?
      document.getElementById("control").style.display = "none"; // hide ctrl tabe
      document.getElementById("setTZ").style.display = "block"; // show setTz
      document.getElementById("setTZbtn").className += " active";
    } else { // view control tab
      document.getElementById("setTZ").style.display = "none"; // hide setTz
      document.getElementById("control").style.display = "block"; // show ctrl tab
      document.getElementById("ctrlbtn" + ctrlNum).className += " active";
      
      // now fetch the control values
      const response = await fetch("getControl?ctrl=" + ctrlNum);
      if (response.ok) {
        var ctrl = await response.json();
        // extract json control parameter values and copy into html for control
        if (ctrl && ctrl.C == ctrlNum) {  // parsed OK and ID matches the ctrl we asked for!
          
          // time
          if (ctrl.T)
            c = Number(ctrl.T) - 1;
          
          // set current state of control indicator
          document.getElementById("I").className = (ctrl.I == 1) ? "on" : "off"
          // ctrl name
          document.getElementById("N").value = ctrl.N.length ? ctrl.N : "Control " + ctrl.C;
          // settings buttons
          setSettings(ctrl.A);
          // set tab state and tab name
          setTabBtn(ctrl.C, ctrl.I);
          setTabBtnName(ctrl.C, ctrl.N);

          // timers
          for (var t = 1; t < maxTimers; t++) { // max timer block
            var timer = ctrl.timers[t];
            var id = "T" + t;
            document.getElementById(id + "O").value = timer.O.substring(0,5); // HH:mm
            document.getElementById(id + "F").value = timer.F;
            if (maxStats) {
              document.getElementById(id + "T").value = timer.T;
              document.getElementById(id + "H").value = timer.H;
              if (timer.L == 'L') {
                document.getElementById(id + "L").options[0].selected = 1;
                document.getElementById(id + "L").options[1].selected = 0;
              } else { // must be morethan - 2nd option
                document.getElementById(id + "L").options[0].selected = 0;
                document.getElementById(id + "L").options[1].selected = 1;
              }
              // stat options, option[0] is -1 for none
              var opt = document.getElementById(id + "S").options;
              for (var c = 0; c < opt.length; c++) // clear any selected options
                opt[c].selected = 0;
              opt[timer.S + 1].selected = 1;
            } else // hide all temp settings cos no stats avail
              document.getElementById(id + "sect").style.display = "none";
          }
        }
      } else {
        errMsg = "ERROR - unable to load control #" + ctrlNum;
      }
    }
  }
  else
    errMsg = "ERROR - invalid control #, " + ctrlNum;
}

// startup stuff
window.onload = function() { 
  const timerTemplate = document.getElementById('timer-template');
  // construct tabs for all control available
  var tz = document.getElementById("setTZbtn");
  for (var c = 0; c < maxCtrls; c++) {
    var btn = document.createElement("button");
    btn.className = "tablinks";
    btn.id = "ctrlbtn" + c;
    btn.setAttribute("onclick", "getControl(" + c + ");");
    btn.innerHTML = "Control " + c;
    tz.insertAdjacentElement("beforebegin", btn);
  }
  // construct timer blocks and stat options list for each timer html
  const timersContainer = document.getElementById('timers-container');
  for (var t = 1; t < maxTimers; t++) {
    const timerClone = timerTemplate.content.cloneNode(true);
    // Replace placeholders like {t} with the actual timer number
    const timerHtml = new XMLSerializer().serializeToString(timerClone).replace(/\{t\}/g, t);
    timersContainer.insertAdjacentHTML('beforeend', timerHtml);

    if (maxStats) {
      for (var o = -1; o < maxStats; o++) {
        var option = document.createElement("option");
        option.value = o; // start at -1 for none
        option.text = (o == -1) ? "none" : statNames[o];
        document.getElementById("T" + t + "S").add(option);
      }
    }
  }

  // listen for return keypress on control form
  document.getElementById("control").addEventListener('keypress', (event) => {
    if (event.key === 'Enter') save(); 
  });  

  getControl(currCtrl);
  getStatus(true); // get status of all ctrls inc. names

  setInterval(getStatus, 2000, false); // refresh timer status every 2 secs
}
