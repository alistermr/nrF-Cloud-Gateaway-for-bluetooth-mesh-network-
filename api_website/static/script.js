const API = "http://localhost:5000"; // Flask backend URL

//Device 1
//const topic = "prod/40e92c8d-1ac8-4b08-a28d-69266969ee2c/m/d/50344654-3037-4bdd-8004-2314d6fc32b9/c2d";
const topic = "prod/40e92c8d-1ac8-4b08-a28d-69266969ee2c/m/d/5034474b-3731-4738-80d4-0c0ffd414431/d2c"


let addressCache = [0]; // index 0 = gateway placeholder, provisioned nodes start at index 1
let nonProvCache = [];
let provCache = [];
let prov_beacon = false;
let selectedDeviceIdx = null;
let scanPollTimer = null;

let cdbData = { nodes: [], subnets: [], appKeys: [] };

const ackWaiters = new Map(); // appId -> { resolve, reject }

function waitForAck(appId, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
            ackWaiters.delete(appId);
            reject(new Error(`Timeout waiting for ack: ${appId}`));
        }, timeoutMs);
        ackWaiters.set(appId, { resolve: (data) => { clearTimeout(timer); resolve(data); }, reject });
    });
}

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

        return res;
    } catch (err) {
        setStatus("error", "Network Error");
        setOutput(err.message);
        return null;
    }
}

async function sendMessagebtn() {
    const msg = document.getElementById("message").value.trim();
    if (!msg) {
        setOutput("Please enter a message.");
        return;
    }

    const btn = document.getElementById("sendBtn");
    btn.disabled = true;
    setOutput("");
    setStatus("pending", "Sending…");
    document.getElementById("output").innerHTML =
        '<span class="spinner"></span> Sending message to board (c2d)…';

    const res = await sendMessage(msg);
    if (!res) {
        btn.disabled = false;
        return;
    }

    const data = await res.json();

    if (res.ok) {
        setStatus("success", "Message sent");
    } else {
        setStatus("error", `${res.status} Error`);
    }

    setOutput(data);
    btn.disabled = false;
}

async function getMessages() {
let last_message_time = new Date().toISOString();
let messageCache = "";
while (true) {
    try {const res = await fetch(API + "/api/get_messages?topic=" + encodeURIComponent(topic) + "&pageLimit=25" + "&start=" + last_message_time);
        const data = await res.json();
        const items = data.response?.items ?? [];
        if (res.ok) {
            //setStatus("success", `${items.length} messages`);
        } else {
            setStatus("error", `${res.status} Error`);
        }

        if (items.length === 0) {
            //console.log("No messages found.");
        } else {
            for (const item of items) {
                console.log("Processing message with appId:", item.message.appId);
                if (ackWaiters.has(item.message.appId)) {
                    const waiter = ackWaiters.get(item.message.appId);
                    ackWaiters.delete(item.message.appId);
                    waiter.resolve(item.message.data);
                }
                if (item.message.appId === "UUID") {
                    getUnprovisionedDevice(item.message.data);
                }
                if (item.message.appId === "init_complete") {
                    document.getElementById("initBtn").style.display = "none";
                    setStatus("success", "Mesh Initialized");
                }
                if (item.message.appId === "Provisioning") {
                    getProvisionedDevice(item.message.data);
                }
                if (item.message.appId === "cdb") {
                    saveCdb(item.message.data);
                }
            }
            console.log("Fetched messages:", items);
            last_message_time = items[0].receivedAt;
            // increase last_message_time by 1ms to avoid fetching the same message again
            last_message_time = new Date(new Date(last_message_time).getTime() + 1).toISOString();
            console.log("Latest message received at:", last_message_time);
            const formatted = items
                .slice()
                .reverse()
                .map(m => {
                    const dataStr = "AppId: " + m.message.appId + ", message: " + m.message.data;
                    return dataStr;
                })
                .join("\n");
            messageCache = formatted + "\n" + messageCache;
        }
        setOutput(messageCache);
    } catch (err) {
        setStatus("error");
        setOutput(err.message);
    }
    await delay(1000);
}
}
getMessages();

async function getUnprovisionedDevice(uuid) {
    if (!nonProvCache.includes(uuid) && !provCache.includes(uuid)) {
        nonProvCache.push(uuid);
        renderDeviceList();
    }
}


async function getProvisionedDevice(msg) {
    console.log("Parsing provisioning message:");
    const re = /Binding complete on app_idx\s+(\d+)\s+and net_idx\s+(\d+)\s+on addresses\s+0x([0-9a-fA-F]{1,4})-0x([0-9a-fA-F]{1,4})/i;
    const m = String(msg).match(re);
    console.log("Regex match result:", m);
    if (!m) return null;
    const app_idx = Number.parseInt(m[1], 10);
    const net_idx = Number.parseInt(m[2], 10);
    let startAddr = Number.parseInt(m[3], 16);
    let endAddr = Number.parseInt(m[4], 16);
    console.log(`Parsed provisioning data - app_idx: ${app_idx}, net_idx: ${net_idx}, startAddr: 0x${startAddr.toString(16)}, endAddr: 0x${endAddr.toString(16)}`);
    console.log("Current addressCache before update:", addressCache);
    for (let addr = startAddr; addr <= endAddr; addr++) {
        addressCache.push([net_idx, app_idx, addr]);
    }
    console.log("Updated addressCache after adding new devices:", addressCache);


    if (!addressCache.includes(startAddr)) {
        addressCache.push(startAddr);
    }
    if (!provCache.includes(startAddr)) {
        provCache.push(startAddr);
    }

    setStatus(
        "success",
        `binding complete on app_idx ${app_idx} and net_idx ${net_idx} on addresses 0x${startAddr.toString(16)}-0x${endAddr.toString(16)}`
    );
    renderProvisionedList();
}


function renderProvisionedList() {
    const list = document.getElementById("provisionedList");
    const cache = addressCache.slice(1);

    if (cache.length === 0) {
        list.innerHTML = '<div class="no-devices">No provisioned devices yet.</div>';
        return;
    }

    list.innerHTML = "";

    cache.forEach((addr, i) => {
        const el = document.createElement("div");
        el.className = "device-item";
        el.innerHTML =
            `<span><span class="device-index">#${i + 1}</span>` +
            `<span class="device-uuid">0x${addr[2].toString(16)} (net_idx: ${addr[0]}, app_idx: ${addr[1]})</span></span>` +
            `<button class="btn-device" onclick="toggle_light(${addr[0]}, ${addr[1]}, ${addr[2]}, true)">On</button>` +
            `<button class="btn-device" onclick="toggle_light(${addr[0]}, ${addr[1]}, ${addr[2]}, false)">Off</button>`;
        list.appendChild(el);
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

async function saveCdb(data) {
    const line = data.trim();
    if (!line) return;

    // Skip header/separator/summary lines
    if (
        line.startsWith("Mesh Network") ||
        line.startsWith("===") ||
        line.startsWith("---") ||
        line.startsWith("Address") ||
        line.startsWith("NetIdx") ||
        line.startsWith(">")
    ) return;

    const fields = line.split(/\s+/);

    if (fields.length === 5 && /^0x[0-9a-fA-F]+$/i.test(fields[0])) {
        // Node: Address  Elements  Flags  UUID  DevKey
        const node = {
            address: fields[0],
            elements: parseInt(fields[1], 10),
            flags: fields[2],
            uuid: fields[3],
            devKey: fields[4],
        };
        const exists = cdbData.nodes.some(n => n.address === node.address);
        if (!exists) cdbData.nodes.push(node);

        // Sync address into provCache / addressCache if not gateway (0x0001)
        const addr = parseInt(node.address, 16);
        if (addr !== 0x0001) {
            for (let i = 0; i < node.elements; i++) {
                const elemAddr = addr + i;
                const already = addressCache.slice(1).some(e => e[2] === elemAddr);
                if (!already) {
                    addressCache.push([0, 0, elemAddr]);
                }
            }
            if (!provCache.includes(addr)) provCache.push(addr);
        }
        renderProvisionedList();

    } else if (fields.length === 2 && /^0x[0-9a-fA-F]+$/i.test(fields[0]) && /^[0-9a-fA-F]{32}$/i.test(fields[1])) {
        // Subnet: NetIdx  NetKey
        const subnet = { netIdx: fields[0], netKey: fields[1] };
        const exists = cdbData.subnets.some(s => s.netIdx === subnet.netIdx);
        if (!exists) cdbData.subnets.push(subnet);

    } else if (fields.length === 3 && /^0x[0-9a-fA-F]+$/i.test(fields[0]) && /^0x[0-9a-fA-F]+$/i.test(fields[1]) && /^[0-9a-fA-F]{32}$/i.test(fields[2])) {
        // App-key: NetIdx  AppIdx  AppKey
        const appKey = { netIdx: fields[0], appIdx: fields[1], appKey: fields[2] };
        const exists = cdbData.appKeys.some(k => k.netIdx === appKey.netIdx && k.appIdx === appKey.appIdx);
        if (!exists) cdbData.appKeys.push(appKey);
    }

    console.log("CDB state:", JSON.stringify(cdbData, null, 2));
}

async function replaceNode() {
        if (cdbData.nodes.length === 0) {
            await sendMessage("No cdb Data");
            return;
        }
        await sendMessage("replace netkey:" + cdbData.subnets.map(s => s.netKey).join(",") +
        " appkey:" + cdbData.appKeys.map(k => k.appKey).join(","));
        await waitForAck("reProvisioned");
        for (const node of cdbData.nodes) {
            if((node.address === "0x0001") || (node.uuid === "dddd0000000000000000000000000000")) continue; // skip gateway
            await sendMessage(`node uuid:${node.uuid} devkey:${node.devKey}`);
            await waitForAck(`node provisioned:${node.uuid}`);
        }
}

async function provisionSelected() {
    if (selectedDeviceIdx === null) return;

    const net_idx = document.getElementById("provNetIdx").value;
    const app_idx = document.getElementById("provAppIdx").value;

    const uuid = nonProvCache[selectedDeviceIdx];
    document.getElementById("provisionBtn").disabled = true;
    setStatus("pending", `Provisioning…`);
    await sendMessage(`prov ${uuid} ${net_idx} ${app_idx}`);

    renderProvisionedList();

    nonProvCache.splice(selectedDeviceIdx, 1);
    selectedDeviceIdx = null;
    renderDeviceList();
}

async function toggle_prov_beacon() {
    await sendMessage("scan");
    if (prov_beacon) {
        prov_beacon = false;
    } else {
        prov_beacon = true;
    }
}

async function toggleScan() {
    const btn = document.getElementById("scanBtn");

    await toggle_prov_beacon();

    if (prov_beacon) {
        btn.textContent = "⏹ Stop";
        btn.classList.add("scanning");
        setStatus("pending", "Scanning…");
        document.getElementById("output").innerHTML =
            '<span class="spinner"></span> Scanning for unprovisioned devices…';
    } else {
        btn.textContent = "🔍 Scan";
        btn.classList.remove("scanning");
        setStatus("success", "Scan stopped");
    }
}

async function toggle_light(net_idx, app_idx, addr, on) {
    if (on) {
        await sendMessage(`light ${net_idx} ${app_idx} ${addr} 1`);
    } else {
        await sendMessage(`light ${net_idx} ${app_idx} ${addr} 0`);
    }
}