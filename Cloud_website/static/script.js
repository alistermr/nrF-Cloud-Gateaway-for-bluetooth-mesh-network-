const API =
  window.location.hostname === "localhost" ||
  window.location.hostname === "127.0.0.1"
    ? "http://localhost:5000"
    : "https://nrfgateaway.onrender.com"; //Flask backend URL

//Device 1
//const topic = "prod/40e92c8d-1ac8-4b08-a28d-69266969ee2c/m/d/50344654-3037-4bdd-8004-2314d6fc32b9/c2d";
const topic = "prod/40e92c8d-1ac8-4b08-a28d-69266969ee2c/m/d/5034474b-3731-4738-80d4-0c0ffd414431/d2c"


let addressCache = [0]; // index 0 = gateway placeholder, provisioned nodes start at index 1
let nonProvCache = [];
let provCache = [];
let prov_beacon = false;
let selectedDeviceIdx = null;
let scanPollTimer = null;
const deviceStates = new Map();

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

function normalizeAddress(addr) {
    if (typeof addr === "number") {
        return `0x${addr.toString(16).padStart(4, "0")}`;
    }

    const text = String(addr || "").trim();
    if (!text) return "0x0000";

    if (text.startsWith("0x") || text.startsWith("0X")) {
        return `0x${text.slice(2).toLowerCase().padStart(4, "0")}`;
    }

    const parsed = Number.parseInt(text, 10);
    if (Number.isNaN(parsed)) {
        return "0x0000";
    }

    return `0x${parsed.toString(16).padStart(4, "0")}`;
}

function getDeviceKey(addr) {
    return normalizeAddress(addr);
}

function getDeviceStatus(addr) {
    return deviceStates.get(getDeviceKey(addr)) || "off";
}

function setDeviceStatus(addr, status) {
    deviceStates.set(getDeviceKey(addr), status.toLowerCase() === "on" ? "on" : "off");
}

function getAddressRange(entry) {
    const startAddress = typeof entry[2] === "number"
        ? entry[2]
        : Number.parseInt(normalizeAddress(entry[2]), 16);
    const elementCount = Number.parseInt(entry[3] ?? 1, 10) || 1;
    return {
        startAddress,
        endAddress: startAddress + elementCount - 1,
    };
}

function findProvisionedEntryByAddress(addr) {
    const address = Number.parseInt(normalizeAddress(addr), 16);
    return addressCache.slice(1).find(entry => {
        const range = getAddressRange(entry);
        return address >= range.startAddress && address <= range.endAddress;
    }) || null;
}

function parseOnOffStatusMessage(message) {
    const match = String(message || "").match(/src=(0x[0-9a-fA-F]+)\s+status=(ON|OFF)\b/i);
    if (!match) return null;

    return {
        address: normalizeAddress(match[1]),
        status: match[2].toLowerCase(),
    };
}

function addProvisionedEntity(netIdx, appIdx, addr, numElements = 1, overwriteMetadata = true) {
    const address = normalizeAddress(addr);
    const existing = addressCache.find(entry => normalizeAddress(entry[2]) === address);

    if (!existing) {
        addressCache.push([netIdx, appIdx, Number.parseInt(address, 16), Math.max(1, Number.parseInt(numElements, 10) || 1)]);
    } else {
        if (overwriteMetadata || (existing[0] === 0 && existing[1] === 0)) {
            existing[0] = netIdx;
            existing[1] = appIdx;
        }
        existing[2] = Number.parseInt(address, 16);
        existing[3] = Math.max(1, Number.parseInt(numElements, 10) || existing[3] || 1);
    }

    const elementCount = Math.max(1, Number.parseInt(numElements, 10) || 1);
    const startAddress = Number.parseInt(address, 16);
    for (let index = 0; index < elementCount; index++) {
        const elementAddress = normalizeAddress(startAddress + index);
        if (!deviceStates.has(elementAddress)) {
            deviceStates.set(elementAddress, "off");
        }
    }
}

function chunkArray(items, size) {
    const chunks = [];
    for (let index = 0; index < items.length; index += size) {
        chunks.push(items.slice(index, index + size));
    }
    return chunks;
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
    try {const res = await fetch(API + "/api/get_messages?topic=" + encodeURIComponent(topic) + "&pageLimit=100" + "&start=" + last_message_time);
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
                if (item.message.appId === "OnOff_status") {
                    const statusUpdate = parseOnOffStatusMessage(item.message.data);
                    if (statusUpdate) {
                        setDeviceStatus(statusUpdate.address, statusUpdate.status);
                        renderProvisionedList();
                    }
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
    const numElements = endAddr - startAddr + 1;
    console.log(`Parsed provisioning data - app_idx: ${app_idx}, net_idx: ${net_idx}, startAddr: 0x${startAddr.toString(16)}, endAddr: 0x${endAddr.toString(16)}`);
    console.log("Current addressCache before update:", addressCache);
    addProvisionedEntity(net_idx, app_idx, startAddr, numElements);
    console.log("Updated addressCache after adding new devices:", addressCache);

    setStatus(
        "success",
        `binding complete on app_idx ${app_idx} and net_idx ${net_idx} on addresses 0x${startAddr.toString(16)}-0x${endAddr.toString(16)}`
    );
    renderProvisionedList();
}


function renderProvisionedList() {
    const list = document.getElementById("provisionedList");
    const cache = addressCache.slice(1).map(entry => ({
        netIdx: entry[0],
        appIdx: entry[1],
        address: normalizeAddress(entry[2]),
        numElements: Number.parseInt(entry[3] ?? 1, 10) || 1,
    }));

    if (cache.length === 0) {
        list.innerHTML = '<div class="no-devices">No provisioned devices yet.</div>';
        return;
    }

    list.innerHTML = "";
    list.className = "node-list";

    cache.forEach((device) => {
        const startAddress = Number.parseInt(device.address, 16);
        const element = document.createElement("div");
        element.className = "node-card";

        const buttons = [];
        for (let elementIndex = 0; elementIndex < device.numElements; elementIndex++) {
            const elementAddress = normalizeAddress(startAddress + elementIndex);
            const status = getDeviceStatus(elementAddress);
            buttons.push(`
                <button
                    class="element-button ${status === "on" ? "is-on" : "is-off"}"
                    aria-pressed="${status === "on" ? "true" : "false"}"
                    onclick="toggleProvisionedLight(${device.netIdx}, ${device.appIdx}, '${elementAddress}', '${status}')"
                >
                    <span class="element-button-address">${elementAddress}</span>
                    <span class="element-button-state">${status.toUpperCase()}</span>
                </button>
            `);
        }

        element.innerHTML = `
            <div class="element-header">
                <span class="el-addr">${device.address}</span>
                <span class="el-meta">${device.netIdx},${device.appIdx}</span>
            </div>
            <div class="element-grid">
                ${buttons.join("")}
            </div>
        `;
        list.appendChild(element);
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

        // Sync address into addressCache if not gateway
        if (node.uuid !== "dddd0000000000000000000000000000") {
            addProvisionedEntity(0, 0, node.address, node.elements, false);
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
        const largestAddress = Math.max(...cdbData.nodes.map(n => parseInt(n.address, 16) + (n.elements - 1)));
        const nextAddress = largestAddress + 1;
        await sendMessage("replace netkey:" + cdbData.subnets.map(s => s.netKey).join(",") + " address:" + nextAddress);
        await waitForAck("reProvisioned");
        for (const appkey of cdbData.appKeys) {
            await sendMessage(`add appkey ${appkey.appIdx} ${appkey.appKey}`);
        }
        await sendMessage("");
        console.log(cdbData);
        for (const node of cdbData.nodes) {
            if((node.address === "0x0001") || (node.uuid === "dddd0000000000000000000000000000")) continue; // skip gateway
            console.log(node);
            await sendMessage(`node uuid:${node.uuid} devkey:${node.devKey} address:${node.address} elements:${node.elements} `);
            await waitForAck(`node`);
        }
}

async function provisionSelected() {
    if (selectedDeviceIdx === null) return;

    const net_idx = "0";
    const app_idx = document.getElementById("provAppIdx").value;

    const uuid = nonProvCache[selectedDeviceIdx];
    document.getElementById("provisionBtn").disabled = true;
    setStatus("pending", `Provisioning…`);
    await sendMessage(`provandconfig ${uuid} ${net_idx} ${app_idx}`);

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

async function toggleProvisionedLight(netIdx, appIdx, addr, currentStatus) {
    const nextOn = currentStatus !== "on";
    await toggle_light(netIdx, appIdx, Number.parseInt(addr, 16), nextOn);
}

async function loadLatestCdb() {
    try {
        const cdbBox = document.getElementById("cdbOutput");
        if (!cdbBox) return;

        if (cdbData.nodes.length === 0 && cdbData.subnets.length === 0 && cdbData.appKeys.length === 0) {
            cdbBox.textContent = "No CDB stored yet.";
            return;
        }

        let output = "";

        if (cdbData.subnets.length > 0) {
            output += "Subnets:\n";
            cdbData.subnets.forEach(s => {
                output += `  ${s.netIdx} ${s.netKey}\n`;
            });
            output += "\n";
        }

        if (cdbData.appKeys.length > 0) {
            output += "App Keys:\n";
            cdbData.appKeys.forEach(k => {
                output += `  ${k.netIdx} ${k.appIdx} ${k.appKey}\n`;
            });
            output += "\n";
        }

        if (cdbData.nodes.length > 0) {
            output += "Nodes:\n";
            cdbData.nodes.forEach(n => {
                output += `  ${n.address} ${n.elements} ${n.flags} ${n.uuid} ${n.devKey}\n`;
            });
        }

        cdbBox.textContent = output.trim();

    } catch (err) {
        const cdbBox = document.getElementById("cdbOutput");
        if (cdbBox) {
            cdbBox.textContent = "Error loading CDB: " + err.message;
        }
    }
}

loadLatestCdb();

setInterval(loadLatestCdb, 5000);