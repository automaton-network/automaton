pragma solidity ^0.5.11;

contract KingAutomaton {
  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Initialization
  //////////////////////////////////////////////////////////////////////////////////////////////////

  constructor(uint256 numSlots, uint8 minDifficultyBits, uint256 predefinedMask, uint256 initialDailySupply) public {
    initMining(numSlots, minDifficultyBits, predefinedMask, initialDailySupply);
    initNames();
    initTreasury();

    // Check if we're on a testnet (We will not using predefined mask when going live)
    if (predefinedMask != 0) {
      // If so, fund the owner for debugging purposes.
      mint(msg.sender, 1000000 ether);
    }
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // ERC20 Token
  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Special purpose community managed addresses.
  address treasuryAddress = address(1);
  address exchangeAddress = address(2);

  string public constant name = "Automaton Network Validator Bootstrap";
  string public constant symbol = "AUTO";
  uint8 public constant decimals = 18;
  uint256 public totalSupply = 0;

  // solhint-disable-next-line no-simple-event-func-name
  event Transfer(address indexed _from, address indexed _to, uint256 _value);
  event Approval(address indexed _owner, address indexed _spender, uint256 _value);

  uint256 constant private MAX_uint = 2**256 - 1;
  mapping (address => uint256) public balances;
  mapping (address => mapping (address => uint256)) public allowed;

  function transfer(address _to, uint256 _value) public returns (bool success) {
    require(balances[msg.sender] >= _value);
    balances[msg.sender] -= _value;
    balances[_to] += _value;
    emit Transfer(msg.sender, _to, _value); //solhint-disable-line indent, no-unused-vars
    return true;
  }

  function transferFrom(address _from, address _to, uint256 _value) public returns (bool success) {
    uint256 allowance = allowed[_from][msg.sender];
    require(balances[_from] >= _value && allowance >= _value);
    balances[_to] += _value;
    balances[_from] -= _value;
    if (allowance < MAX_uint) {
        allowed[_from][msg.sender] -= _value;
    }
    emit Transfer(_from, _to, _value); //solhint-disable-line indent, no-unused-vars
    return true;
  }

  function balanceOf(address _owner) public view returns (uint256 balance) {
    return balances[_owner];
  }

  function approve(address _spender, uint256 _value) public returns (bool success) {
    allowed[msg.sender][_spender] = _value;
    emit Approval(msg.sender, _spender, _value); //solhint-disable-line indent, no-unused-vars
    return true;
  }

  function allowance(address _owner, address _spender) public view returns (uint256 remaining) {
    return allowed[_owner][_spender];
  }

  // This is only to be used with special purpose community accounts like Treasury, Exchange.
  // Those accounts help to represent the total supply correctly.
  function transferInternal(address _from, address _to, uint256 _value) private returns (bool success) {
    require(balances[_from] >= _value, "Insufficient balance");
    balances[_to] += _value;
    balances[_from] -= _value;
    emit Transfer(_from, _to, _value); //solhint-disable-line indent, no-unused-vars
    return true;
  }

  function mint(address _receiver, uint256 _value) private {
    balances[_receiver] += _value;
    totalSupply += _value;
    emit Transfer(address(0), _receiver, _value); //solhint-disable-line indent, no-unused-vars
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Treasury
  //////////////////////////////////////////////////////////////////////////////////////////////////

  enum State {
    Proposed,
    Approved,
    Rejected,
    Accepted,
    Terminated,
    Completed
  }

  struct Proposal {
    address payable contributor;
    string title;
    string documentsLink;
    bytes documentsHash;

    uint256 yesVotes;
    uint256 noVotes;
  }

  Proposal[] proposals;

  function initTreasury() private {
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Mining
  //////////////////////////////////////////////////////////////////////////////////////////////////

  event NewSlotKing(uint256 slot, address newOwner);

  struct ValidatorSlot {
    address owner;
    uint256 difficulty;
    uint256 last_claim_time;
  }
  ValidatorSlot[] public slots;

  uint256 public minDifficulty;          // Minimum difficulty
  uint256 public mask;                   // Prevents premine
  uint256 public numTakeOvers;           // Number of times a slot was taken over by a new king.
  uint256 public rewardPerSlotPerSecond; // Validator reward per slot per second.

  function initMining(uint256 numSlots, uint8 minDifficultyBits, uint256 predefinedMask, uint256 initialDailySupply) private {
    require(numSlots > 0);
    require(minDifficultyBits > 0);

    slots.length = numSlots;
    minDifficulty = (2 ** uint256(minDifficultyBits) - 1) << (256 - minDifficultyBits);
    if (predefinedMask == 0) {
      // Prevents premining with a known predefined mask.
      mask = uint256(keccak256(abi.encodePacked(now, msg.sender)));
    } else {
      // Setup predefined mask, useful for testing purposes.
      mask = predefinedMask;
    }

    rewardPerSlotPerSecond = (1 ether * initialDailySupply) / 1 days / numSlots;
  }

  function getSlotsNumber() public view returns(uint256) {
    return slots.length;
  }

  function getSlotOwner(uint256 slot) public view returns(address) {
    return slots[slot].owner;
  }

  function getSlotDifficulty(uint256 slot) public view returns(uint256) {
    return slots[slot].difficulty;
  }

  function getSlotLastClaimTime(uint256 slot) public view returns(uint256) {
    return slots[slot].last_claim_time;
  }

  function getOwners(uint256 start, uint256 len) public view returns(address[] memory result) {
    result = new address[](len);
    for(uint256 i = 0; i < len; i++) {
      result[i] = slots[start + i].owner;
    }
  }

  function getDifficulties(uint256 start, uint256 len) public view returns(uint256[] memory result) {
    result = new uint256[](len);
    for(uint256 i = 0; i < len; i++) {
      result[i] = slots[start + i].difficulty;
    }
  }

  function getLastClaimTimes(uint256 start, uint256 len) public view returns(uint256[] memory result) {
    result = new uint256[](len);
    for(uint256 i = 0; i < len; i++) {
      result[i] = slots[start + i].last_claim_time;
    }
  }

  function getMask() public view returns(uint256) {
    return mask;
  }

  function getClaimed() public view returns(uint256) {
    return numTakeOvers;
  }

  /** Claims slot based on a signature.
    * @param pubKeyX X coordinate of the public key used to claim the slot
    * @param pubKeyY Y coordinate of the public key used to claim the slot
    * @param v recId of the signature needed for ecrecover
    * @param r R portion of the signature
    * @param s S portion of the signature
    */
  function claimSlot(
    bytes32 pubKeyX,
    bytes32 pubKeyY,
    uint8 v,
    bytes32 r,
    bytes32 s
  ) public {
    uint256 slot = uint256(pubKeyX) % slots.length;
    uint256 key = uint256(pubKeyX) ^ mask;

    // Check if the key can take over the slot and become the new king.
    require(key > minDifficulty && key > slots[slot].difficulty, "Low key difficulty");

    // Make sure the signature is valid.
    require(verifySignature(pubKeyX, pubKeyY, bytes32(uint256(msg.sender)), v, r, s), "Signature not valid");

    // TODO(asen): Implement reward decaying over time.

    // Kick out prior king if any and reward them.
    uint256 last_time = slots[slot].last_claim_time;
    if (last_time != 0) {
      require (last_time < now, "mining same slot in same block or clock is wrong");
      uint256 value = (now - last_time) * rewardPerSlotPerSecond;
      mint(address(treasuryAddress), value);
      mint(slots[slot].owner, value);
    } else {
      // Reward first time validators as if they held the slot for 1 hour.
      uint256 value = (3600) * rewardPerSlotPerSecond;
      mint(address(treasuryAddress), value);
      mint(msg.sender, value);
    }

    // Update the slot with data for the new king.
    slots[slot].owner = msg.sender;
    slots[slot].difficulty = key;
    slots[slot].last_claim_time = now;

    numTakeOvers++;
    emit NewSlotKing(slot, msg.sender);
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // User Registration
  //////////////////////////////////////////////////////////////////////////////////////////////////

  struct UserInfo {
    string userName;
    string info;
  }

  mapping (string => address) public mapNameToUser;
  mapping (address => UserInfo) public mapUsersInfo;
  address[] public userAddresses;

  function initNames() private {
    registerUserInternal(treasuryAddress, "Treasury", "");
  }

  function getUserName(address addr) public view returns (string memory) {
    return mapUsersInfo[addr].userName;
  }

  function registerUserInternal(address addr, string memory userName, string memory info) private {
    userAddresses.push(addr);
    mapNameToUser[userName] = addr;
    mapUsersInfo[addr].userName = userName;
    mapUsersInfo[addr].info = info;
  }

  function registerUser(string memory userName, string memory info) public {
    require(mapNameToUser[userName] == address(0));
    registerUserInternal(msg.sender, userName, info);
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // DEX
  //////////////////////////////////////////////////////////////////////////////////////////////////

  uint256 public minOrderETH = 1 ether / 10;
  uint256 public minOrderAUTO = 1000 ether;

  enum OrderType { Buy, Sell, Auction }

  struct Order {
    uint256 AUTO;
    uint256 ETH;
    address payable owner;
    OrderType orderType;
  }

  Order[] public orders;

  function getExchangeBalance() public view returns (uint256) {
    return balanceOf(exchangeAddress);
  }

  function removeOrder(uint256 _id) private {
    orders[_id] = orders[orders.length - 1];
    orders.length--;
  }

  function getOrdersLength() public view returns (uint256) {
    return orders.length;
  }

  function buy(uint256 _AUTO) public payable returns (uint256 _id) {
    require(msg.value >= minOrderETH, "Minimum ETH requirement not met");
    require(_AUTO >= minOrderAUTO, "Minimum AUTO requirement not met");
    _id = orders.length;
    orders.push(Order(_AUTO, msg.value, msg.sender, OrderType.Buy));
  }

  function sellNow(uint256 _id, uint256 _AUTO, uint256 _ETH) public {
    require(_id < orders.length, "Invalid Order ID");
    Order memory o = orders[_id];
    require(o.AUTO == _AUTO, "Order AUTO does not match requested size");
    require(o.ETH == _ETH, "Order ETH does not match requested size");
    require(o.orderType == OrderType.Buy, "Invalid order type");
    transfer(o.owner, _AUTO);
    msg.sender.transfer(_ETH);
    removeOrder(_id);
  }

  function sell(uint256 _AUTO, uint256 _ETH) public returns (uint256 _id){
    require(_AUTO >= minOrderAUTO, "Minimum AUTO requirement not met");
    require(_ETH >= minOrderETH, "Minimum ETH requirement not met");
    transfer(exchangeAddress, _AUTO);
    _id = orders.length;
    orders.push(Order(_AUTO, _ETH, msg.sender, OrderType.Sell));
  }

  function buyNow(uint256 _id, uint256 _AUTO) public payable {
    require(_id < orders.length, "Invalid Order ID");
    Order memory o = orders[_id];
    require(o.AUTO == _AUTO, "Order AUTO does not match requested size");
    require(o.ETH == msg.value, "Order ETH does not match requested size");
    require(o.orderType == OrderType.Sell, "Invalid order type");
    o.owner.transfer(msg.value);
    transferInternal(exchangeAddress, msg.sender, _AUTO);
    removeOrder(_id);
  }

  function cancelOrder(uint256 _id) public {
    Order memory o = orders[_id];
    require(o.owner == msg.sender);

    if (o.orderType == OrderType.Buy) {
      msg.sender.transfer(o.ETH);
    }

    if (o.orderType == OrderType.Sell) {
      transferInternal(exchangeAddress, msg.sender, o.AUTO);
    }

    removeOrder(_id);
  }

  //////////////////////////////////////////////////////////////////////////////////////////////////
  // Internal Helpers
  //////////////////////////////////////////////////////////////////////////////////////////////////

  // Returns the Ethereum address corresponding to the input public key.
  function getAddressFromPubKey(bytes32 pubkeyX, bytes32 pubkeyY) private pure returns (uint256) {
    return uint256(keccak256(abi.encodePacked(pubkeyX, pubkeyY))) & 0x00FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF;
  }

  // Verifies that signature of a message matches the given public key.
  function verifySignature(bytes32 pubkeyX, bytes32 pubkeyY, bytes32 hash,
      uint8 v, bytes32 r, bytes32 s) private pure returns (bool) {
    uint256 addr = getAddressFromPubKey(pubkeyX, pubkeyY);
    address addr_r = ecrecover(hash, v, r, s);
    return addr == uint256(addr_r);
  }
}
