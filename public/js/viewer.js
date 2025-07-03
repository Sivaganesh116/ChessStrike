const charToReason = {
  r: "Repitition",
  c: "Checkmate",
  t: "Timeout",
  a: "Agreement",
  f: "Fifty Half Moves",
  s: "Stalemate",
  A: "Abandonment",
  R: "Resignation"
};


const boardElementId = 'board';
const movesTbody = document.getElementById('moves-tbody');
const prevBtn = document.getElementById('prev-btn');
const nextBtn = document.getElementById('next-btn');
let currentTurn = 'w';
let isBottomWhite = true;

let board = null;
let moveHistory = ['start']; // FEN history
let timeStamps = [];
let currentMoveIndex = 0;

let enpassantSquare = null;

function updateNavButtons() {
    prevBtn.disabled = currentMoveIndex <= 0;
    nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
}

function updateTimersAt(index) {
    const ts = timeStamps[index] || [600, 600];

    if(isBottomWhite) {
        document.getElementById('bottom-timer').innerText = formatTime(ts[0]);
        document.getElementById('top-timer').innerText = formatTime(ts[1]);
    }
    else {
        document.getElementById('bottom-timer').innerText = formatTime(ts[1]);
        document.getElementById('top-timer').innerText = formatTime(ts[0]);
    }
}

function formatTime(seconds) {
    const m = Math.floor(seconds / 60);
    const s = seconds % 60;
    return `${m}:${s.toString().padStart(2, '0')}`;
}

function updateMoveHistory(san, color, fen, index) {
    moveHistory.push(fen);
    const moveNumber = Math.ceil(moveHistory.length / 2);

    if (color === 'w') {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td class="p-1">${moveNumber}</td>
            <td class="p-1 font-semibold cursor-pointer" data-move-index="${index}">${san}</td>
            <td class="p-1 font-semibold"></td>
        `;
        movesTbody.appendChild(row);
    } else {
        const lastRow = movesTbody.lastElementChild;
        if (lastRow) {
            const cell = lastRow.children[2];
            cell.textContent = san;
            cell.classList.add('cursor-pointer');
            cell.dataset.moveIndex = index;
        }
    }

    movesTbody.parentElement.scrollTop = movesTbody.parentElement.scrollHeight;
}

function insertGameResultBlock(result, reason, whiteUsername, blackUsername) {
    const rightPanel = document.querySelector('.bg-white.p-4.rounded-lg.shadow-lg.flex.flex-col');
    if (!rightPanel) return;

    const resultDiv = document.createElement('div');
    resultDiv.className = 'mt-4 p-4 bg-yellow-100 border-l-4 border-yellow-500 rounded shadow';

    const title = document.createElement('h3');
    title.className = 'text-xl font-bold mb-2 text-yellow-800';
    title.textContent = 'Game Result';
    resultDiv.appendChild(title);

    const resultPara = document.createElement('p');
    resultPara.className = 'text-gray-800 font-medium';

    if (result === "d") {
        resultPara.textContent = `Draw by ${reason}`;
    } else if (result === "w") {
        resultPara.textContent = `${whiteUsername} won by ${reason}`;
    } else if (result === "b") {
        resultPara.textContent = `${blackUsername} won by ${reason}`;
    } else {
        resultPara.textContent = `Result unavailable`;
    }

    resultDiv.appendChild(resultPara);

    // Insert before first <hr> in right panel (usually above Actions)
    const allChildren = rightPanel.children;
    for (let i = 0; i < allChildren.length; i++) {
        if (allChildren[i].tagName === 'HR') {
            rightPanel.insertBefore(resultDiv, allChildren[i]);
            return;
        }
    }

    rightPanel.appendChild(resultDiv); // fallback
}

function updateTimersDisplay() {
    const bottomTimer = document.getElementById('bottom-timer');
    const topTimer = document.getElementById('top-timer');

    if(isBottomWhite) {
        bottomTimer.textContent = whiteTime;
        topTimer.textContent = blackTime;
    }
    else {
        bottomTimer.textContent = blackTime;
        topTimer.textContent = whiteTime;
    }
}

function changeOrientation() {
    let bottomNameEle = document.getElementById('bottom-name')
    let topNameEle = document.getElementById('top-name');
    
    const bottomName = bottomNameEle.innerText;

    bottomNameEle.innerText = topNameEle.innerText;
    topNameEle.innerText = bottomName;

    isBottomWhite = !isBottomWhite;

    updateTimersDisplay();

    board.orientation(isBottomWhite ? 'white' : 'black');
}

function setupEventListeners() {
    prevBtn.addEventListener('click', () => {
        if (currentMoveIndex > 0) {
            currentMoveIndex--;
            board.position(moveHistory[currentMoveIndex], false);
            updateTimersAt(currentMoveIndex);
            updateNavButtons();
            currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
        }
    });

    nextBtn.addEventListener('click', () => {
        if (currentMoveIndex < moveHistory.length - 1) {
            currentMoveIndex++;
            board.position(moveHistory[currentMoveIndex], false);
            updateTimersAt(currentMoveIndex);
            updateNavButtons();
            currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
        }
    });

    movesTbody.addEventListener('click', (e) => {
        const target = e.target.closest('[data-move-index]');
        if (target) {
            const idx = parseInt(target.dataset.moveIndex, 10);
            if (!isNaN(idx)) {
                currentMoveIndex = idx+1;
                board.position(moveHistory[currentMoveIndex], false);
                updateTimersAt(idx);
                updateNavButtons();
                currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
            }
        }
    });
}

function onDragStart(source, piece) {
    if(currentTurn != piece.charAt(0)) return false;
}

function onDrop(source, target) {
    const position = board.position();
    if(position[target] && position[target].charAt(0) == currentTurn) return 'snapback';

    currentTurn = currentTurn == 'w' ? 'b' : 'w';
}

function isCastle(move, madeOnBoard) {
    const from = move.substring(0, 2);
    const to = move.substring(2, 4);

    const position = board.position();
    const piece = madeOnBoard ? position[to] :position[from];

    if(!piece || piece[1].toLowerCase() !== 'k') return false;

    if (from === 'e1' && to === 'g1') {
        if(!madeOnBoard) board.move('e1-g1', 'h1-f1');
        else board.move('h1-f1');

        return true;
    } else if (from === 'e1' && to === 'c1') {
        if(!madeOnBoard) board.move('e1-c1', 'a1-d1');
        else board.move('a1-d1');

        return true;
    } else if (from === 'e8' && to === 'g8') {
        if(!madeOnBoard) board.move('e8-g8', 'h8-f8');
        else board.move('h8-f8');

        return true;
    } else if (from === 'e8' && to === 'c8') {
        if(!madeOnBoard) board.move('e8-c8', 'a8-d8');
        else board.move('a8-d8');

        return true;
    }

    return false;
}

function makeMove(move, madeOnBoard) {
    console.log(move);
    let source = move.substring(0, 2);
    let target = move.substring(2, 4);

    let position = board.position();
    const piece = position[source];
    const color = piece.charAt(0);

    if(move.length == 5) {
        position[source] = null;
        position[target] =  color + move[4].toUpperCase();

        board.position(position, false);
    }
    else if(isCastle(move, madeOnBoard)) {
        board.position(board.position(), false);
    }
    else if(piece.charAt(1) === 'P' && target == enpassantSquare) {
        let rank = parseInt(target[1], 10);
        let captureSquare = target[0] + (color == 'w' ? (rank - 1) : (rank + 1));

        position[captureSquare] = null;
        position[source] = null;
        position[target] = piece;

        board.position(position, false);
    }
    else {
        board.move(`${move.substring(0, 2)}-${move.substring(2, 4)}`);
        board.position(board.position(), false);
    }

    // update enpassant square
    if (piece.charAt(1) === 'P' && Math.abs(parseInt(source[1]) - parseInt(target[1])) === 2) {
        const middleRank = (parseInt(source[1]) + parseInt(target[1])) / 2;
        enpassantSquare = target[0] + middleRank;
    }
    else enpassantSquare = null;
}

function initBoard() {
    const config = {
        draggable: true,
        position: 'start',
        pieceTheme: '/img/chesspieces/wikipedia/{piece}.png',
        onDragStart: onDragStart,
        onDrop: onDrop,
        dropOffBoard: 'snapback',
        moveSpeed: 0,
        snapbackSpeed: 0,
        snapSpeed: 0,
        appearSpeed: 0,
        highlight: false,
        onError: "console"
    };
    board = Chessboard(boardElementId, config);
}

async function loadGame() {
    const path = window.location.pathname;
    const gameID = path.slice(path.lastIndexOf('/') + 1);

    const res = await fetch(`/game-info/${gameID}`);
    if (!res.ok) throw new Error('Failed to fetch game info');
    const gameData = await res.json();

    insertGameResultBlock(gameData.result, charToReason[gameData.reason], gameData.white, gameData.black);


    // Display player names
    const whiteNameElem = document.getElementById('bottom-name');
    const blackNameElem = document.getElementById('top-name');
    whiteNameElem.innerHTML = `<a href="/player/${gameData.white}" class="text-blue-600 hover:underline">${gameData.white}</a>`;
    blackNameElem.innerHTML = `<a href="/player/${gameData.black}" class="text-blue-600 hover:underline">${gameData.black}</a>`;

    // Apply moves
    if(gameData.moves.length) {
        const moves = gameData.moves.split(' ');
        moves.forEach((move, idx) => {
            makeMove(move, false);
            updateMoveHistory(move, idx%2 == 0 ? "w" : "b", board.fen(), idx);
        });
    }

    const stamps = gameData.timeStamps.split(' ');
    stamps.forEach((stamp, idx) => {
        timeStamps.push([parseInt(stamp.substring(0, stamp.indexOf('-') + 1)), parseInt(stamp.substring(stamp.indexOf('-') + 1, stamp.length))]);
    });

    currentMoveIndex = 0;

    board.position(moveHistory[0], false);
    updateTimersAt(0);
    updateNavButtons();
}

initBoard();
setupEventListeners();
loadGame();
