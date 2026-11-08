(* The reading of tag 28 (shareable) and tag 29 (sharedref) of the value-sharing registration,
   http://cbor.schmorp.de/value-sharing, over the items of RFC 8949 in their preorder.
   Every function takes values and gives a value; nothing is changed in place. *)

From Stdlib Require Import List Arith Lia PeanoNat.
Import ListNotations.

(* The heads of a stream, head before content (RFC 8949 3). *)
Inductive tok :=
| TUInt (n : nat)
| TLeaf (n : nat)
| TArr (k : nat)
| TMap (k : nat)
| TTag (t : nat).

Definition shareable := 28.
Definition sharedref := 29.

Inductive node :=
| NUInt (n : nat)
| NLeaf (n : nat)
| NArr (children : list nat)
| NMap (pairs : list (nat * nat))
| NTag (t : nat) (content : nat).

Record state := mkstate {
  next : nat;                 (* the id of the next node *)
  table : list nat;           (* entry n: the node that the n-th tag 28 marks *)
  graph : list (nat * node);  (* the complete nodes, the newest first *)
  steps : nat                 (* the heads that the reader has looked at *)
}.

Inductive err :=
| out_of_fuel
| too_little_data
| sharedref_index_not_unsigned
| sharedref_index_not_marked
| sharedref_not_defined.

Inductive result (A : Type) :=
| ok (value : A)
| fail (e : err).
Arguments ok {A}.
Arguments fail {A}.

Definition made (id : nat) (n : node) (s : state) : state :=
  mkstate (next s) (table s) ((id, n) :: graph s) (steps s).

Definition reserved (s : state) : state :=
  mkstate (S (next s)) (table s) (graph s) (steps s).

(* A chain 28(28(...)): the count of tags 28 that follow and the stream after them. One pass over the chain. *)
Fixpoint chain (ts : list tok) (c : nat) : nat * list tok :=
  match ts with
  | TTag t :: rest => if Nat.eqb t shareable then chain rest (S c) else (c, ts)
  | _ => (c, ts)
  end.

(* The node that the c marks of a chain name: the item after the chain. If that item makes a node, it gets the
   next id. If it is a tag 29, the chain marks the value that the tag 29 names; a tag 29 that names a mark of the
   chain itself names a value that is not defined. *)
Definition chain_target (content : list tok) (c : nat) (s : state) : result nat :=
  match content with
  | TTag t :: rest =>
    if Nat.eqb t sharedref then
      match rest with
      | TUInt i :: _ =>
        match nth_error (table s) i with
        | Some id => if id <? next s then ok id else fail sharedref_not_defined
        | None => if i <? length (table s) + c then fail sharedref_not_defined else fail sharedref_index_not_marked
        end
      | [] => fail too_little_data
      | _ :: _ => fail sharedref_index_not_unsigned
      end
    else ok (next s)
  | _ => ok (next s)
  end.

Definition marked_as (id : nat) (c : nat) (s : state) : state :=
  mkstate (next s) (table s ++ repeat id c) (graph s) (steps s).

(* One look at a head. A tag 29 is one look at its head and one at its index. *)
Definition looked (n : nat) (s : state) : state :=
  mkstate (next s) (table s) (graph s) (n + steps s).

(* items reads k items one after the other with the reader of one item, and gives their ids in order. *)
Fixpoint items (rd : list tok -> state -> result (nat * list tok * state)) (k : nat) (ts : list tok) (s : state)
  : result (list nat * list tok * state) :=
  match k with
  | 0 => ok ([], ts, s)
  | S k' =>
    match rd ts s with
    | fail e => fail e
    | ok (id, rest, s1) =>
      match items rd k' rest s1 with
      | fail e => fail e
      | ok (ids, rest', s2) => ok (id :: ids, rest', s2)
      end
    end
  end.

(* pairs takes the ids of the keys and the values of a map, key before value. *)
Fixpoint pairs (l : list nat) : list (nat * nat) :=
  match l with
  | a :: b :: l' => (a, b) :: pairs l'
  | _ => []
  end.

(* read_item reads one item and gives the id of its node, the rest of the stream and the new state.
   A node gets its id at its head, before its content (the specification: "record the reference before
   decoding the value"), and enters the graph once, when it is complete. A tag 28 makes no node: its entry
   is the id that the content gets. A tag 29 makes no node: it gives the id of an entry. *)
Fixpoint read_item (fuel : nat) (ts : list tok) (s : state) {struct fuel}
  : result (nat * list tok * state) :=
  match fuel with
  | 0 => fail out_of_fuel
  | S fuel' =>
    let read_items := items (read_item fuel') in
    match ts with
    | [] => fail too_little_data
    | TUInt n :: rest => ok (next s, rest, made (next s) (NUInt n) (reserved (looked 1 s)))
    | TLeaf n :: rest => ok (next s, rest, made (next s) (NLeaf n) (reserved (looked 1 s)))
    | TArr k :: rest =>
      match read_items k rest (reserved (looked 1 s)) with
      | fail e => fail e
      | ok (ids, rest', s1) => ok (next s, rest', made (next s) (NArr ids) s1)
      end
    | TMap k :: rest =>
      match read_items (k + k) rest (reserved (looked 1 s)) with
      | fail e => fail e
      | ok (ids, rest', s1) =>
        ok (next s, rest', made (next s) (NMap (pairs ids)) s1)
      end
    | TTag t :: rest =>
      if Nat.eqb t shareable then
        let (c, content) := chain rest 1 in
        match chain_target content c s with
        | fail e => fail e
        | ok target => read_item fuel' content (marked_as target c (looked c s))
        end
      else if Nat.eqb t sharedref then
        match rest with
        | TUInt i :: rest' =>
          match nth_error (table s) i with
          | None => fail sharedref_index_not_marked
          | Some id => if id <? next s then ok (id, rest', looked 2 s) else fail sharedref_not_defined
          end
        | [] => fail too_little_data
        | _ :: _ => fail sharedref_index_not_unsigned
        end
      else
        match read_item fuel' rest (reserved (looked 1 s)) with
        | fail e => fail e
        | ok (c, rest', s1) => ok (next s, rest', made (next s) (NTag t c) s1)
        end
    end
  end.

Definition start : state := mkstate 0 [] [] 0.

Definition read (ts : list tok) : result (nat * state) :=
  match read_item (S (length ts)) ts start with
  | fail e => fail e
  | ok (root, [], s) => ok (root, s)
  | ok (_, _ :: _, _) => fail too_little_data
  end.

(* What every step keeps: the stream gets shorter, the graph and the table only grow at their ends, and the
   next id only grows. *)
Definition extends (s s' : state) : Prop :=
  (exists g, graph s' = g ++ graph s) /\ (exists t, table s' = table s ++ t) /\ next s <= next s'.

Lemma extends_refl : forall s, extends s s.
Proof.
  intros s; split; [exists []; reflexivity | split; [exists []; rewrite app_nil_r; reflexivity | lia]].
Qed.

Lemma extends_trans : forall a b c, extends a b -> extends b c -> extends a c.
Proof.
  intros a b c [[g1 G1] [[t1 T1] N1]] [[g2 G2] [[t2 T2] N2]].
  split; [exists (g2 ++ g1); rewrite G2, G1, app_assoc; reflexivity |].
  split; [exists (t1 ++ t2); rewrite T2, T1, app_assoc; reflexivity | lia].
Qed.

Lemma extends_reserved : forall s, extends s (reserved s).
Proof. intros s; split; [exists []; reflexivity | split; [exists []; rewrite app_nil_r; reflexivity | simpl; lia]]. Qed.

Lemma extends_marked_as : forall id c s, extends s (marked_as id c s).
Proof. intros id c s; split; [exists []; reflexivity | split; [exists (repeat id c); reflexivity | simpl; lia]]. Qed.

Lemma chain_shorter : forall ts c c' content, chain ts c = (c', content) -> length content <= length ts /\ c <= c'.
Proof.
  induction ts as [| t ts IH]; intros c c' content H; simpl in H.
  - inversion H; subst; simpl; lia.
  - destruct t as [| | | | tg]; try (inversion H; subst; simpl; lia).
    destruct (Nat.eqb tg shareable).
    + apply IH in H. simpl. lia.
    + inversion H; subst; simpl; lia.
Qed.

Lemma chain_content_head : forall ts c c' content,
  chain ts c = (c', content) -> forall rest, content <> TTag shareable :: rest.
Proof.
  induction ts as [| t ts IH]; intros c c' content H; simpl in H.
  - inversion H; subst; intros rest E; discriminate.
  - destruct t as [| | | | tg]; try (inversion H; subst; intros rest E; discriminate).
    destruct (Nat.eqb tg shareable) eqn:M.
    + eapply IH; eassumption.
    + inversion H; subst. intros rest E. injection E as Et _. subst tg.
      unfold shareable in M. discriminate.
Qed.

Lemma extends_looked : forall n s, extends s (looked n s).
Proof. intros; split; [exists []; reflexivity | split; [exists []; rewrite app_nil_r; reflexivity | simpl; lia]]. Qed.

Lemma extends_rl : forall s, extends s (reserved (looked 1 s)).
Proof. intros; split; [exists []; reflexivity | split; [exists []; rewrite app_nil_r; reflexivity | simpl; lia]]. Qed.

Lemma extends_made : forall id n s, extends s (made id n s).
Proof. intros; split; [exists [(id, n)]; reflexivity | split; [exists []; rewrite app_nil_r; reflexivity | simpl; lia]]. Qed.

(* A property of one read that also holds for a run of reads holds for items. *)
Lemma items_step :
  forall (rd : list tok -> state -> result (nat * list tok * state)) (P : state -> list tok -> state -> list tok -> Prop),
    (forall s ts, P s ts s ts) ->
    (forall a b c x y z, P a x b y -> P b y c z -> P a x c z) ->
    (forall ts s id rest s', rd ts s = ok (id, rest, s') -> P s ts s' rest) ->
    forall k ts s ids rest s', items rd k ts s = ok (ids, rest, s') -> P s ts s' rest.
Proof.
  intros rd P Prefl Ptrans Prd.
  induction k as [| k IH]; intros ts s ids rest s' H; simpl in H.
  - inversion H; subst; apply Prefl.
  - destruct (rd ts s) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items rd k rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst.
    eapply Ptrans; [eapply Prd; exact E1 | eapply IH; exact E2].
Qed.

(* T5 (no node changes after it is made) and the growth of the table: a read only adds. *)
Theorem read_item_extends :
  forall fuel ts s id rest s',
    read_item fuel ts s = ok (id, rest, s') -> extends s s' /\ length rest < length ts.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  destruct t as [n | n | k | k | tg].
  - inversion H; subst; split; [eapply extends_trans; [apply extends_rl | apply extends_made] | simpl; lia].
  - inversion H; subst; split; [eapply extends_trans; [apply extends_rl | apply extends_made] | simpl; lia].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    destruct (items_step (read_item fuel) (fun a x b y => extends a b /\ length y <= length x)
                (fun s0 ts0 => conj (extends_refl s0) (le_n _))
                (fun a b c x y z H1 H2 => conj (extends_trans _ _ _ (proj1 H1) (proj1 H2)) (Nat.le_trans _ _ _ (proj2 H2) (proj2 H1)))
                (fun ts0 s0 id0 r0 s0' E0 => let (X0, L0) := IH _ _ _ _ _ E0 in conj X0 (Nat.lt_le_incl _ _ L0))
                _ _ _ _ _ _ E) as [X L].
    split; [eapply extends_trans; [apply extends_rl | eapply extends_trans; [exact X | apply extends_made]] | simpl; lia].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    destruct (items_step (read_item fuel) (fun a x b y => extends a b /\ length y <= length x)
                (fun s0 ts0 => conj (extends_refl s0) (le_n _))
                (fun a b c x y z H1 H2 => conj (extends_trans _ _ _ (proj1 H1) (proj1 H2)) (Nat.le_trans _ _ _ (proj2 H2) (proj2 H1)))
                (fun ts0 s0 id0 r0 s0' E0 => let (X0, L0) := IH _ _ _ _ _ E0 in conj X0 (Nat.lt_le_incl _ _ L0))
                _ _ _ _ _ _ E) as [X L].
    split; [eapply extends_trans; [apply extends_rl | eapply extends_trans; [exact X | apply extends_made]] | simpl; lia].
  - destruct (Nat.eqb tg shareable) eqn:M.
    + destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      destruct (IH _ _ _ _ _ H) as [X L].
      apply chain_shorter in Ch.
      split; [eapply extends_trans; [eapply extends_trans; [apply extends_looked | apply extends_marked_as] | exact X] | simpl; lia].
    + destruct (Nat.eqb tg sharedref) eqn:R.
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i) as [x |]; [| discriminate].
        destruct (x <? next s); [| discriminate].
        inversion H; subst; split; [apply extends_looked | simpl; lia].
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst.
        destruct (IH _ _ _ _ _ E) as [X L].
        split; [eapply extends_trans; [apply extends_rl | eapply extends_trans; [exact X | apply extends_made]] | simpl; lia].
Qed.

(* T1 (it ends): the fuel of read, one more than the length of the stream, is never used up. Each read of an
   item takes at least one head, so the fuel counts heads. *)
Lemma items_fuel :
  forall (rd : list tok -> state -> result (nat * list tok * state)) n,
    (forall ts s, length ts <= n -> rd ts s <> fail out_of_fuel) ->
    (forall ts s id rest s', rd ts s = ok (id, rest, s') -> length rest <= length ts) ->
    forall k ts s, length ts <= n -> items rd k ts s <> fail out_of_fuel.
Proof.
  intros rd n Hrd Hlen.
  induction k as [| k IH]; intros ts s L H; simpl in H; [discriminate |].
  destruct (rd ts s) as [[[id1 rest1] s1] | e] eqn:E1.
  - destruct (items rd k rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [discriminate |].
    inversion H; subst.
    eapply IH; [| exact E2].
    specialize (Hlen _ _ _ _ _ E1); lia.
  - inversion H; subst. eapply Hrd; [exact L | exact E1].
Qed.

Lemma chain_target_fuel : forall ts c s, chain_target ts c s <> fail out_of_fuel.
Proof.
  intros ts c s H; unfold chain_target in H.
  destruct ts as [| [| | | | tg] ts]; try discriminate.
  destruct (Nat.eqb tg sharedref); [| discriminate].
  destruct ts as [| [i | | | |] ts']; try discriminate.
  destruct (nth_error (table s) i); [destruct (_ <? _); discriminate |].
  destruct (_ <? _); discriminate.
Qed.

Theorem read_item_fuel : forall fuel ts s, length ts < fuel -> read_item fuel ts s <> fail out_of_fuel.
Proof.
  induction fuel as [| fuel IH]; intros ts s L H; [simpl in L; lia |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  simpl in L.
  assert (Hit : forall k ts0 s0, length ts0 <= length ts -> items (read_item fuel) k ts0 s0 <> fail out_of_fuel).
  { intros k ts0 s0 L0.
    apply (items_fuel (read_item fuel) (length ts)); [| | exact L0].
    - intros ts1 s1 L1. apply IH. lia.
    - intros ts1 s1 id1 r1 s1' E. apply Nat.lt_le_incl. eapply read_item_extends; exact E. }
  destruct t as [n | n | k | k | tg].
  - discriminate.
  - discriminate.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E.
    + discriminate.
    + inversion H; subst. eapply Hit; [| exact E]. lia.
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E.
    + discriminate.
    + inversion H; subst. eapply Hit; [| exact E]. lia.
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content] eqn:Ch.
      apply chain_shorter in Ch.
      destruct (chain_target content c s) as [target | e] eqn:MT.
      * eapply IH; [| exact H]. lia.
      * inversion H; subst. eapply chain_target_fuel; eassumption.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _); discriminate.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E.
        -- discriminate.
        -- inversion H; subst. eapply IH; [| exact E]. lia.
Qed.

(* T4 (no expansion): each node that read makes takes a head of its own, and every reserved id enters the
   graph once. So the graph has at most as many nodes as the stream has heads. *)
Definition counted (a : state) (x : list tok) (b : state) (y : list tok) : Prop :=
  next b + length y <= next a + length x /\ length (graph b) + next a = length (graph a) + next b.

Lemma counted_refl : forall s ts, counted s ts s ts.
Proof. intros; unfold counted; lia. Qed.

Lemma counted_trans : forall a b c x y z, counted a x b y -> counted b y c z -> counted a x c z.
Proof. unfold counted; intros; lia. Qed.

Theorem read_item_counted :
  forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> counted s ts s' rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> counted s0 ts0 s0' r0).
  { intros. eapply (items_step (read_item fuel) counted counted_refl counted_trans); [| eassumption].
    intros. eapply IH; eassumption. }
  unfold counted in *.
  destruct t as [n | n | k | k | tg].
  - inversion H; subst; simpl; lia.
  - inversion H; subst; simpl; lia.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply Hit in E. unfold counted in E. simpl in *. lia.
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply Hit in E. unfold counted in E. simpl in *. lia.
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content] eqn:Ch.
      apply chain_shorter in Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      apply IH in H. unfold counted in H. simpl in *. lia.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _); [| discriminate].
        inversion H; subst; simpl; lia.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. apply IH in E. unfold counted in E. simpl in *. lia.
Qed.

Theorem read_no_expansion :
  forall ts root s, read ts = ok (root, s) -> length (graph s) = next s /\ next s <= length ts.
Proof.
  intros ts root s H. unfold read in H.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate].
  inversion H; subst.
  apply read_item_counted in E. unfold counted in E. simpl in E. lia.
Qed.

(* T3 (no reference outside the graph): the id that a read gives is reserved, every reserved id is a node of
   the graph once its read is complete, and the children of every node are reserved ids. *)
Definition children (n : node) : list nat :=
  match n with
  | NArr ids => ids
  | NMap ps => flat_map (fun p => [fst p; snd p]) ps
  | NTag _ c => [c]
  | _ => []
  end.

Lemma read_item_id_reserved :
  forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> id < next s'.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  destruct t as [n | n | k | k | tg].
  - inversion H; subst; simpl; lia.
  - inversion H; subst; simpl; lia.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. simpl.
    assert (X : extends (reserved (looked 1 s)) s1).
    { eapply (items_step (read_item fuel) (fun a _ b _ => extends a b)); [| | | exact E].
      - intros; apply extends_refl.
      - intros; eapply extends_trans; eassumption.
      - intros; eapply read_item_extends; eassumption. }
    destruct X as [_ [_ N]]. simpl in N. lia.
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. simpl.
    assert (X : extends (reserved (looked 1 s)) s1).
    { eapply (items_step (read_item fuel) (fun a _ b _ => extends a b)); [| | | exact E].
      - intros; apply extends_refl.
      - intros; eapply extends_trans; eassumption.
      - intros; eapply read_item_extends; eassumption. }
    destruct X as [_ [_ N]]. simpl in N. lia.
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content].
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      eapply IH; exact H.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _) eqn:L; [| discriminate].
        inversion H; subst. apply Nat.ltb_lt in L. exact L.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. simpl.
        destruct (read_item_extends _ _ _ _ _ _ E) as [[_ [_ N]] _]. simpl in N. lia.
Qed.

Lemma items_ids_reserved :
  forall fuel k ts s ids rest s',
    items (read_item fuel) k ts s = ok (ids, rest, s') -> Forall (fun i => i < next s') ids.
Proof.
  intros fuel. induction k as [| k IH]; intros ts s ids rest s' H; simpl in H.
  - inversion H; subst; constructor.
  - destruct (read_item fuel ts s) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
    destruct (items (read_item fuel) k rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
    inversion H; subst.
    constructor.
    + apply read_item_id_reserved in E1.
      assert (X : extends s1 s').
      { eapply (items_step (read_item fuel) (fun a _ b _ => extends a b)); [| | | exact E2].
        - intros; apply extends_refl.
        - intros; eapply extends_trans; eassumption.
        - intros; eapply read_item_extends; eassumption. }
      destruct X as [_ [_ N]]. lia.
    + eapply IH; exact E2.
Qed.

Definition complete_between (a b : state) : Prop :=
  forall i, next a <= i < next b -> exists n, In (i, n) (graph b).

Definition sound (s : state) : Prop :=
  forall i n, In (i, n) (graph s) -> i < next s /\ Forall (fun c => c < next s) (children n).

Lemma in_extends : forall a b i n, extends a b -> In (i, n) (graph a) -> In (i, n) (graph b).
Proof. intros a b i n [[g G] _] H. rewrite G. apply in_or_app. right. exact H. Qed.

Lemma complete_trans :
  forall a b c, extends b c -> next a <= next b -> complete_between a b -> complete_between b c -> complete_between a c.
Proof.
  intros a b c X N Hab Hbc i Hi.
  destruct (Nat.lt_ge_cases i (next b)) as [L | L].
  - destruct (Hab i (conj (proj1 Hi) L)) as [n Hn]. exists n. eapply in_extends; eassumption.
  - apply Hbc. lia.
Qed.

Lemma pairs_children : forall P ids, Forall P ids -> Forall P (children (NMap (pairs ids))).
Proof.
  intros P ids. simpl.
  assert (G : forall m l, length l <= m -> Forall P l -> Forall P (flat_map (fun p => [fst p; snd p]) (pairs l))).
  { induction m as [| m IH]; intros l L F.
    - destruct l; [constructor | simpl in L; lia].
    - destruct l as [| a [| b l']]; simpl; [constructor | constructor |].
      inversion F as [| ? ? Pa F1]; subst. inversion F1 as [| ? ? Pb F2]; subst.
      constructor; [exact Pa |]. constructor; [exact Pb |].
      apply IH; [simpl in L; lia | exact F2]. }
  intros F. apply (G (length ids)); [lia | exact F].
Qed.

Theorem read_item_sound :
  forall fuel ts s id rest s',
    read_item fuel ts s = ok (id, rest, s') -> sound s -> sound s' /\ complete_between s s'.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H S; [discriminate |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') ->
            sound s0 -> sound s0' /\ complete_between s0 s0' /\ extends s0 s0').
  { induction k as [| k IHk]; intros ts0 s0 ids r0 s0' Hk S0; simpl in Hk.
    - inversion Hk; subst. split; [exact S0 | split; [intros i Hi; lia | apply extends_refl]].
    - destruct (read_item fuel ts0 s0) as [[[id1 rest1] s1] | e] eqn:E1; [| discriminate].
      destruct (items (read_item fuel) k rest1 s1) as [[[ids2 rest2] s2] | e] eqn:E2; [| discriminate].
      inversion Hk; subst.
      destruct (IH _ _ _ _ _ E1 S0) as [S1 C1].
      destruct (read_item_extends _ _ _ _ _ _ E1) as [X1 _].
      destruct (IHk _ _ _ _ _ E2 S1) as [S2 [C2 X2]].
      split; [exact S2 | split; [eapply complete_trans; eauto; apply X1 | eapply extends_trans; eauto]]. }
  assert (Made : forall s1 n0, extends (reserved (looked 1 s)) s1 -> sound s1 -> complete_between (reserved (looked 1 s)) s1 ->
            Forall (fun c => c < next s1) (children n0) ->
            sound (made (next s) n0 s1) /\ complete_between s (made (next s) n0 s1)).
  { intros s1 n0 X S1 C1 F. destruct X as [_ [_ N]]. simpl in N.
    split.
    - intros i n [E | E]; simpl.
      + inversion E; subst. split; [lia | exact F].
      + exact (S1 i n E).
    - intros i Hi. simpl in Hi.
      destruct (Nat.eq_dec i (next s)) as [-> | D].
      + exists n0. left. reflexivity.
      + destruct (C1 i) as [n Hn]; [simpl; lia |]. exists n. right. exact Hn. }
  assert (Rs : sound (reserved (looked 1 s))).
  { intros i n Hi. destruct (S i n Hi) as [L F]. simpl. split; [lia | refine (Forall_impl _ _ F); intros; lia]. }
  destruct t as [n | n | k | k | tg].
  - inversion H; subst. apply (Made (reserved (looked 1 s))); [apply extends_refl | exact Rs | intros i Hi; simpl in Hi; lia | constructor].
  - inversion H; subst. apply (Made (reserved (looked 1 s))); [apply extends_refl | exact Rs | intros i Hi; simpl in Hi; lia | constructor].
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    destruct (Hit _ _ _ _ _ _ E Rs) as [S1 [C1 X1]].
    apply Made; [exact X1 | exact S1 | exact C1 | eapply items_ids_reserved; exact E].
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst.
    destruct (Hit _ _ _ _ _ _ E Rs) as [S1 [C1 X1]].
    apply Made; [exact X1 | exact S1 | exact C1 | apply pairs_children; eapply items_ids_reserved; exact E].
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content].
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      assert (Sm : sound (marked_as target c (looked c s))) by (intros i n Hi; exact (S i n Hi)).
      destruct (IH _ _ _ _ _ H Sm) as [S1 C1]. split; [exact S1 |].
      intros i Hi. apply C1. simpl. exact Hi.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _); [| discriminate].
        inversion H; subst. split; [exact S | intros j Hj; simpl in Hj; lia].
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst.
        destruct (IH _ _ _ _ _ E Rs) as [S1 C1].
        destruct (read_item_extends _ _ _ _ _ _ E) as [X1 _].
        apply Made; [exact X1 | exact S1 | exact C1 |].
        constructor; [eapply read_item_id_reserved; exact E | constructor].
Qed.

Theorem read_no_dangling :
  forall ts root s, read ts = ok (root, s) ->
    root < next s /\ sound s /\ (forall i, i < next s -> exists n, In (i, n) (graph s)).
Proof.
  intros ts root s H. unfold read in H.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate].
  inversion H; subst.
  split; [eapply read_item_id_reserved; exact E |].
  assert (S0 : sound start) by (intros i n []).
  destruct (read_item_sound _ _ _ _ _ _ E S0) as [S1 C1].
  split; [exact S1 | intros i Hi; apply C1; simpl; lia].
Qed.

(* T2 (numbering): a tag 28 adds one entry at the end of the table, and that entry is the node of the item that
   the tag marks: the item after a chain of tags 28, or the node that a tag 29 at the end of the chain names. *)
Lemma nth_error_app_prefix : forall (l t : list nat) i x, nth_error l i = Some x -> nth_error (l ++ t) i = Some x.
Proof.
  intros l t i x H. rewrite nth_error_app1; [exact H |]. apply nth_error_Some. rewrite H. discriminate.
Qed.

Lemma chain_target_is_node :
  forall fuel content c s s1 t id rest s',
    chain_target content c s = ok t ->
    (forall rest0, content <> TTag shareable :: rest0) ->
    next s1 = next s ->
    (exists m, table s1 = table s ++ m) ->
    read_item fuel content s1 = ok (id, rest, s') ->
    id = t.
Proof.
  intros [| fuel] content c s s1 t id rest s' MT NM N [m Tm] H; [discriminate |].
  simpl in H.
  destruct content as [| tk ts]; [discriminate |].
  destruct tk as [n | n | k | k | tg]; simpl in MT.
  - inversion MT; inversion H; subst; congruence.
  - inversion MT; inversion H; subst; congruence.
  - inversion MT; subst.
    destruct (items (read_item fuel) k ts (reserved (looked 1 s1))) as [[[ids r1] s2] | e]; [| discriminate].
    inversion H; subst; congruence.
  - inversion MT; subst.
    destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s1))) as [[[ids r1] s2] | e]; [| discriminate].
    inversion H; subst; congruence.
  - destruct (Nat.eqb tg shareable) eqn:M.
    + apply Nat.eqb_eq in M. subst tg. exfalso. eapply NM. reflexivity.
    + destruct (Nat.eqb tg sharedref) eqn:R.
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i) as [x |] eqn:Ex.
        -- destruct (x <? next s) eqn:Lx; [| discriminate].
           inversion MT; subst.
           rewrite Tm in H. rewrite (nth_error_app_prefix _ _ _ _ Ex) in H.
           rewrite N in H. rewrite Lx in H. inversion H; subst. reflexivity.
        -- destruct (_ <? _); discriminate.
      * inversion MT; subst.
        destruct (read_item fuel ts (reserved (looked 1 s1))) as [[[c0 r1] s2] | e]; [| discriminate].
        inversion H; subst; congruence.
Qed.

Theorem mark_names_its_item :
  forall fuel ts s id rest s',
    read_item (S fuel) (TTag shareable :: ts) s = ok (id, rest, s') ->
    nth_error (table s') (length (table s)) = Some id.
Proof.
  intros fuel ts s id rest s' H. simpl in H.
  destruct (chain ts 1) as [c content] eqn:Ch.
  destruct (chain_target content c s) as [t | e] eqn:MT; [| discriminate].
  assert (C1 : 1 <= c) by (apply chain_shorter in Ch; lia).
  assert (I : id = t).
  { eapply (chain_target_is_node fuel content c s (marked_as t c (looked c s))); [exact MT | | reflexivity | | exact H].
    - eapply chain_content_head; exact Ch.
    - exists (repeat t c). reflexivity. }
  subst t.
  destruct (read_item_extends _ _ _ _ _ _ H) as [[_ [[m Tm] _]] _].
  rewrite Tm. simpl. rewrite <- app_assoc.
  rewrite nth_error_app2; [| lia]. rewrite Nat.sub_diag.
  destruct c as [| c']; [lia |]. reflexivity.
Qed.

(* A tag 29 gives the entry that its index names, and makes no node. *)
Theorem sharedref_names_entry :
  forall fuel i ts s id rest s',
    read_item (S fuel) (TTag sharedref :: TUInt i :: ts) s = ok (id, rest, s') ->
    nth_error (table s) i = Some id /\ s' = looked 2 s /\ rest = ts.
Proof.
  intros fuel i ts s id rest s' H. simpl in H.
  destruct (nth_error (table s) i) as [x |] eqn:E; [| discriminate].
  destruct (x <? next s); [| discriminate].
  inversion H; subst. auto.
Qed.

(* Cases with their result, computed by the model. Each one is a case of the specification or a trap that a
   reader met; the name says which. *)
Definition result_of (ts : list tok) : result (nat * list (nat * node) * list nat) :=
  match read ts with
  | ok (root, s) => ok (root, graph s, table s)
  | fail e => fail e
  end.

(* The specification: [28([]), 29(0), []], the first two elements are one array. *)
Example spec_shared_array :
  result_of [TArr 3; TTag 28; TArr 0; TTag 29; TUInt 0; TArr 0]
  = ok (0, [(0, NArr [1; 1; 2]); (2, NArr []); (1, NArr [])], [1]).
Proof. reflexivity. Qed.

(* The specification: 28([29(0)]), an array that holds itself. *)
Example spec_cyclic_array :
  result_of [TTag 28; TArr 1; TTag 29; TUInt 0] = ok (0, [(0, NArr [0])], [0]).
Proof. reflexivity. Qed.

(* A cycle through a tag that is not 28: 28(1234([29(0)])). *)
Example cycle_through_tag :
  result_of [TTag 28; TTag 1234; TArr 1; TTag 29; TUInt 0]
  = ok (0, [(0, NTag 1234 1); (1, NArr [0])], [0]).
Proof. reflexivity. Qed.

(* A mark whose content is a reference to itself names no value: 28(29(0)). *)
Example mark_of_itself : result_of [TTag 28; TTag 29; TUInt 0] = fail sharedref_not_defined.
Proof. reflexivity. Qed.

(* The same through a chain of marks: 28(28(29(0))) and 28(28(29(1))). *)
Example chain_of_itself_outer : result_of [TTag 28; TTag 28; TTag 29; TUInt 0] = fail sharedref_not_defined.
Proof. reflexivity. Qed.
Example chain_of_itself_inner : result_of [TTag 28; TTag 28; TTag 29; TUInt 1] = fail sharedref_not_defined.
Proof. reflexivity. Qed.

(* A mark of a reference marks the value that the reference names: [28(1), 28(29(0)), 29(1)]. *)
Example mark_of_reference :
  result_of [TArr 3; TTag 28; TUInt 1; TTag 28; TTag 29; TUInt 0; TTag 29; TUInt 1]
  = ok (0, [(0, NArr [1; 1; 1]); (1, NUInt 1)], [1; 1]).
Proof. reflexivity. Qed.

(* Two marks of one value: 28(28(5)), then 29(0) and 29(1) name the same node. *)
Example two_marks_one_value :
  result_of [TArr 3; TTag 28; TTag 28; TUInt 5; TTag 29; TUInt 0; TTag 29; TUInt 1]
  = ok (0, [(0, NArr [1; 1; 1]); (1, NUInt 5)], [1; 1]).
Proof. reflexivity. Qed.

(* A reference before any mark: [29(0)]. *)
Example forward_reference : result_of [TArr 1; TTag 29; TUInt 0] = fail sharedref_index_not_marked.
Proof. reflexivity. Qed.

(* A reference to a mark that comes later: [29(0), 28(1)]. *)
Example reference_before_its_mark :
  result_of [TArr 2; TTag 29; TUInt 0; TTag 28; TUInt 1] = fail sharedref_index_not_marked.
Proof. reflexivity. Qed.

(* The content of a tag 29 that is no unsigned integer: 29("x"). *)
Example reference_over_text : result_of [TTag 29; TLeaf 0] = fail sharedref_index_not_unsigned.
Proof. reflexivity. Qed.

(* A map whose value refers to its own key: {28("a"): 29(0)}. *)
Example value_refers_to_key :
  result_of [TMap 1; TTag 28; TLeaf 7; TTag 29; TUInt 0] = ok (0, [(0, NMap [(1, 1)]); (1, NLeaf 7)], [1]).
Proof. reflexivity. Qed.

(* A map that holds itself as its key: 28({29(0): 1}). *)
Example map_is_its_own_key :
  result_of [TTag 28; TMap 1; TTag 29; TUInt 0; TUInt 1] = ok (0, [(0, NMap [(0, 1)]); (1, NUInt 1)], [0]).
Proof. reflexivity. Qed.

(* T1 (linear cost): the reader looks at each head once. A chain of tags 28 is one pass, a tag 29 looks at its
   head and its index, and nothing is read twice. The lookup of an entry in the table is one step of the C++
   reader, which keeps the table in a std::vector; this list model does not count it. *)
Lemma chain_length : forall ts c c' content, chain ts c = (c', content) -> length content + c' = length ts + c.
Proof.
  induction ts as [| t ts IH]; intros c c' content H; simpl in H.
  - inversion H; subst; reflexivity.
  - destruct t as [| | | | tg]; try (inversion H; subst; reflexivity).
    destruct (Nat.eqb tg shareable).
    + apply IH in H. simpl. lia.
    + inversion H; subst; reflexivity.
Qed.

Definition linear (a : state) (x : list tok) (b : state) (y : list tok) : Prop :=
  steps b + length y = steps a + length x.

Theorem read_item_linear :
  forall fuel ts s id rest s', read_item fuel ts s = ok (id, rest, s') -> linear s ts s' rest.
Proof.
  induction fuel as [| fuel IH]; intros ts s id rest s' H; [discriminate |].
  simpl in H.
  destruct ts as [| t ts]; [discriminate |].
  assert (Hit : forall k ts0 s0 ids r0 s0', items (read_item fuel) k ts0 s0 = ok (ids, r0, s0') -> linear s0 ts0 s0' r0).
  { intros. eapply (items_step (read_item fuel) linear); [| | | eassumption].
    - intros; unfold linear; reflexivity.
    - unfold linear; intros; lia.
    - intros. eapply IH; eassumption. }
  unfold linear in *.
  destruct t as [n | n | k | k | tg].
  - inversion H; subst; simpl; lia.
  - inversion H; subst; simpl; lia.
  - destruct (items (read_item fuel) k ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply Hit in E. unfold linear in E. simpl in *. lia.
  - destruct (items (read_item fuel) (k + k) ts (reserved (looked 1 s))) as [[[ids r1] s1] | e] eqn:E; [| discriminate].
    inversion H; subst. apply Hit in E. unfold linear in E. simpl in *. lia.
  - destruct (Nat.eqb tg shareable).
    + destruct (chain ts 1) as [c content] eqn:Ch.
      destruct (chain_target content c s) as [target | e]; [| discriminate].
      apply chain_length in Ch.
      apply IH in H. unfold linear in H. simpl in *. lia.
    + destruct (Nat.eqb tg sharedref).
      * destruct ts as [| [i | | | |] ts']; try discriminate.
        destruct (nth_error (table s) i); [| discriminate].
        destruct (_ <? _); [| discriminate].
        inversion H; subst; simpl; lia.
      * destruct (read_item fuel ts (reserved (looked 1 s))) as [[[c r1] s1] | e] eqn:E; [| discriminate].
        inversion H; subst. apply IH in E. unfold linear in E. simpl in *. lia.
Qed.

Theorem read_linear : forall ts root s, read ts = ok (root, s) -> steps s = length ts.
Proof.
  intros ts root s H. unfold read in H.
  destruct (read_item (S (length ts)) ts start) as [[[r rest] s'] | e] eqn:E; [| discriminate].
  destruct rest; [| discriminate].
  inversion H; subst.
  apply read_item_linear in E. unfold linear in E. simpl in E. lia.
Qed.
