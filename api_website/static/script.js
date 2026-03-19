
const API = ""; // same origin — served by Flask
let addressCache = [];
let nonProvCache = [];

function delay(ms) {
return new Promise(resolve => setTimeout(resolve, ms));
}

function setStatus(type, text) {
    const badge = document.getElementById("statusBadge");
    badge.style.display = "inline-block";
    badge.className = "status-badge " + type;
    badge.textContent = text;
}

function setOutput(content) {
    document.getElementById("output").textContent =
    typeof content === "string" ? content : JSON.stringify(content, null, 2);
}

async function sendMessage(message) {
    try {
    const res = await fetch(API + "/api/send", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ message }),
    });
    return res
    await delay(1000); // simulate network delay
    } catch (err) {
    setStatus("error", "Network Error");
    setOutput(err.message);
    }
}

async function sendMessagebtn() {
    const msg = document.getElementById("message").value.trim();
    if (!msg) { setOutput("Please enter a message."); return; }

    const btn = document.getElementById("sendBtn");
    btn.disabled = true;
    setOutput("");
    setStatus("pending", "Sending…");
    document.getElementById("output").innerHTML = '<span class="spinner"></span> Sending message to board (c2d)…';
    const res = await sendMessage(msg);
    if (!res) return; // error already handled in sendMessage

    const data = await res.json();
    if (res.ok) {
    setStatus("success", `${res.status} OK`);
    } else {
        setStatus("error", `${res.status} Error`);
    }
    setStatus("success", "Message sent");
    setOutput(data);
    btn.disabled = false;
}

async function getDeviceState() {
    setOutput("");
    setStatus("pending", "Fetching…");
    document.getElementById("output").innerHTML = '<span class="spinner"></span> Fetching device state…';

    try {
    const res = await fetch(API + "/api/device");
    const data = await res.json();

    if (res.ok) {
        setStatus("success", `${res.status} OK`);
    } else {
        setStatus("error", `${res.status} Error`);
    }

    setOutput(data);
    } catch (err) {
    setStatus("error", "Network Error");
    setOutput(err.message);
    }
}

async function listMessages() {
    setOutput("");
    setStatus("pending", "Fetching…");
    document.getElementById("output").innerHTML = '<span class="spinner"></span> Loading message history…';

    try {
    const res = await fetch(API + "/api/messages", {

    });
    const data = await res.json();

    if (res.ok) {
        setStatus("success", `${data.response?.total ?? 0} messages`);
    } else {
        setStatus("error", `${res.status} Error`);
    }

    setOutput(data);
    } catch (err) {
    setStatus("error", "Network Error");
    setOutput(err.message);
    }
}

async function mesh_init(){
    setOutput("");
    setStatus("pending", "Initializing mesh");
    document.getElementById("output").innerHTML = '<span class="spinner"></span> Initializing mesh network…';

    document.getElementById("initBtn").style.display = "none";
    await sendMessage("mesh init");
    await delay(1000);
    await sendMessage("mesh cdb create")
    await delay(1000);
    await sendMessage("mesh prov local 0 0x0001")
    addressCache[0] = "0x0001";

    setStatus("success", "Mesh Initialized");
    document.getElementById("output").innerHTML = "Mesh network initialized.";
}

let prov_beacon = false;
let selectedDeviceIdx = null;

async function toggle_prov_beacon() {
    if (prov_beacon){
    await sendMessage("mesh prov beacon-listen off");
    prov_beacon = false;
    return;
    }else{
    await sendMessage("mesh prov beacon-listen on");
    //for testing
    nonProvCache[0] = "0622b315eab04703b9dd4cea154fc8fc";
    prov_beacon = true;
    }
}

function renderProvisionedList() {
    const list = document.getElementById("provisionedList");
    const cache = addressCache.splice(1); // skip local provisioner at 0x0001
    if (cache.length === 0) {
    list.innerHTML = '<div class="no-devices">No provisioned devices yet.</div>';
    return;
    }

    list.innerHTML = "";
    cache.forEach((addr, i) => {
    for (u = 0; u < 4; u++) {
        const el = document.createElement("div");
        el.className = "device-item";
        el.innerHTML =
        `<span><span class="device-index">#${i*4+u}</span>` +
        `<span class="device-uuid">${addr + u}</span></span>` +
        `<button class="btn-device" onclick="toggle_light('${addr + u}', true)">On</button>` +
        `<button class="btn-device" onclick="toggle_light('${addr + u}', false)" style="background:#ef4444;color:#fff;">Off</button>`;
        list.appendChild(el);
    }

    


    });
}

function renderDeviceList() {
    const list = document.getElementById("deviceList");
    const provBtn = document.getElementById("provisionBtn");

    if (nonProvCache.length === 0) {
    list.innerHTML = '<div class="no-devices">No unprovisioned devices found. Click Scan to start.</div>';
    selectedDeviceIdx = null;
    provBtn.disabled = true;
    return;
    }

    list.innerHTML = "";
    nonProvCache.forEach((uuid, i) => {
    const el = document.createElement("div");
    el.className = "device-item" + (selectedDeviceIdx === i ? " selected" : "");
    el.innerHTML =
        `<span><span class="device-index">#${i}</span>` +
        `<span class="device-uuid">${uuid}</span></span>`;
    el.onclick = () => selectDevice(i);
    list.appendChild(el);
    });
}

function selectDevice(idx) {
    selectedDeviceIdx = idx;
    document.getElementById("provisionBtn").disabled = false;
    renderDeviceList();
}

async function toggleScan() {
    const btn = document.getElementById("scanBtn");
    await toggle_prov_beacon();

    if (prov_beacon) {
    btn.textContent = "⏹ Stop";
    btn.classList.add("scanning");
    } else {
    btn.textContent = "🔍 Scan";
    btn.classList.remove("scanning");
    }

    renderDeviceList();
}

//assumes provising generic onoff on nrf54l15
async function provisionSelected() {
    if (selectedDeviceIdx === null) return;
    const uuid = nonProvCache[selectedDeviceIdx];
    const nextAddrNum = addressCache.length * 0x10;
    const toHex = (n) => "0x" + n.toString(16).padStart(4, "0");
    const nextAddr = toHex(nextAddrNum);

    setStatus("pending", "Provisioning…" + nextAddr);
    document.getElementById("output").innerHTML =
    '<span class="spinner"></span> Provisioning ' + uuid + '…';

    await sendMessage(`mesh prov remote ${uuid} 0 ${nextAddr} 5`);

    // move to mesh init
    //------------------
    await sendMessage("mesh target dst local");
    await sendMessage(`mesh target net 0`);
    await sendMessage("mesh models cfg appkey add 0 0");
    //---------------

    await sendMessage(`mesh target dst ${nextAddr}`);
    await sendMessage("mesh target net 0"); //maybe redundant
    await sendMessage("mesh models cfg appkey add 0 0");
    await sendMessage(`mesh models cfg model app-bind ${nextAddr} 0 0x1000`); // bind to generic onoff client
    await sendMessage(`mesh models cfg model app-bind ${toHex(nextAddrNum + 1)} 0 0x1000`);
    await sendMessage(`mesh models cfg model app-bind ${toHex(nextAddrNum + 2)} 0 0x1000`);
    await sendMessage(`mesh models cfg model app-bind ${toHex(nextAddrNum + 3)} 0 0x1000`);
    
    addressCache.push(nextAddr);
    renderProvisionedList();

    // Remove from unprovisioned list
    nonProvCache.splice(selectedDeviceIdx, 1);
    selectedDeviceIdx = null;
    document.getElementById("provisionBtn").disabled = true;
    renderDeviceList();

    setStatus("success", "Provisioned " + nextAddr);
    setOutput(`Device ${uuid} provisioned at address ${nextAddr}`);
}

async function toggle_light(addr, on) {
    await sendMessage(`mesh target dst ${addr}`);
    await sendMessage("mesh target net 0"); // redundant?
    await sendMessage("mesh target app 0"); //redundant?
    if (on) {
    await sendMessage(`mesh test net-send 82020100`);
    } else {
    await sendMessage(`mesh test net-send 82020000`);
    }
}

// Initial poll + interval
//pollMessages();
//setInterval(pollMessages, 5000);