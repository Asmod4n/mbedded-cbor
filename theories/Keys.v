(* Map keys compared as values. Two nodes are the same key when their unfoldings agree at every depth. The
   decision compares the unfoldings to a depth that the sizes of the two graphs bound. *)

From Stdlib Require Import List Arith Lia PeanoNat Bool.
From MbeddedCbor Require Import ValueSharing Writer.
Import ListNotations.

Definition bisimilar (g1 : list (nat * node)) (a : nat) (g2 : list (nat * node)) (b : nat) : Prop :=
  forall k, unfold g1 k a = unfold g2 k b.

Fixpoint all2 {A B : Type} (R : A -> B -> bool) (l1 : list A) (l2 : list B) : bool :=
  match l1, l2 with
  | [], [] => true
  | x :: l1', y :: l2' => R x y && all2 R l1' l2'
  | _, _ => false
  end.

(* Two nodes agree in kind and data, and R holds for their children in order. *)
Definition node_eqb (R : nat -> nat -> bool) (n1 n2 : node) : bool :=
  match n1, n2 with
  | NUInt x, NUInt y => Nat.eqb x y
  | NLeaf x, NLeaf y => Nat.eqb x y
  | NArr l1, NArr l2 => all2 R l1 l2
  | NMap l1, NMap l2 => all2 (fun p q => R (fst p) (fst q) && R (snd p) (snd q)) l1 l2
  | NTag t1 c1, NTag t2 c2 => Nat.eqb t1 t2 && R c1 c2
  | _, _ => false
  end.

(* eq_upto g1 g2 k a b tells whether the unfoldings of depth k agree (see eq_upto_unfold). *)
Fixpoint eq_upto (g1 g2 : list (nat * node)) (k : nat) (a b : nat) : bool :=
  match k with
  | 0 => true
  | S k' =>
    match assoc g1 a, assoc g2 b with
    | Some n1, Some n2 => node_eqb (eq_upto g1 g2 k') n1 n2
    | None, None => true
    | _, _ => false
    end
  end.

Definition same_key (g1 : list (nat * node)) (a : nat) (g2 : list (nat * node)) (b : nat) : bool :=
  eq_upto g1 g2 (length g1 * length g2) a b.

Lemma all2_map : forall A B C (R : A -> B -> bool) (f : A -> C) (h : B -> C),
  (forall x y, R x y = true <-> f x = h y) ->
  forall l1 l2, all2 R l1 l2 = true <-> map f l1 = map h l2.
Proof.
  intros A B C R f h H. induction l1 as [| x l1 IH]; intros [| y l2]; simpl;
    try (split; intros E; [discriminate E | discriminate E]); [split; reflexivity |].
  rewrite andb_true_iff, H, IH. split.
  - intros [E1 E2]. rewrite E1, E2. reflexivity.
  - intros E. injection E as E1 E2. split; assumption.
Qed.

Lemma eq_upto_unfold : forall g1 g2 k a b, eq_upto g1 g2 k a b = true <-> unfold g1 k a = unfold g2 k b.
Proof.
  intros g1 g2. induction k as [| k IH]; intros a b; [simpl; split; reflexivity |].
  simpl. destruct (assoc g1 a) as [n1 |], (assoc g2 b) as [n2 |].
  - destruct n1, n2; simpl; try (split; intros E; discriminate E).
    + rewrite Nat.eqb_eq. split; [intros ->; reflexivity | intros E; injection E; auto].
    + rewrite Nat.eqb_eq. split; [intros ->; reflexivity | intros E; injection E; auto].
    + rewrite (all2_map _ _ _ _ _ _ IH). split; [intros ->; reflexivity | intros E; injection E; auto].
    + rewrite (all2_map _ _ _ _ (fun p => (unfold g1 k (fst p), unfold g1 k (snd p)))
                              (fun q => (unfold g2 k (fst q), unfold g2 k (snd q)))).
      * split; [intros ->; reflexivity | intros E; injection E; auto].
      * intros p q. rewrite andb_true_iff, IH, IH. split.
        -- intros [E1 E2]. rewrite E1, E2. reflexivity.
        -- intros E. injection E as E1 E2. split; assumption.
    + rewrite andb_true_iff, Nat.eqb_eq, IH. split.
      * intros [-> E]. rewrite E. reflexivity.
      * intros E. injection E as E1 E2. split; assumption.
  - destruct n1; simpl; split; intros E; discriminate E.
  - destruct n2; simpl; split; intros E; discriminate E.
  - split; reflexivity.
Qed.

Lemma all2_mono : forall A B (R R' : A -> B -> bool),
  (forall x y, R x y = true -> R' x y = true) -> forall l1 l2, all2 R l1 l2 = true -> all2 R' l1 l2 = true.
Proof.
  intros A B R R' H. induction l1 as [| x l1 IH]; intros [| y l2]; simpl; auto.
  rewrite !andb_true_iff. intros [E1 E2]. split; [apply H; exact E1 | apply IH; exact E2].
Qed.

Lemma eq_upto_down : forall g1 g2 k a b, eq_upto g1 g2 (S k) a b = true -> eq_upto g1 g2 k a b = true.
Proof.
  intros g1 g2. induction k as [| k IH]; intros a b H; [reflexivity |].
  simpl in H |- *. destruct (assoc g1 a) as [n1 |], (assoc g2 b) as [n2 |]; try exact H.
  destruct n1, n2; simpl in H |- *; try exact H.
  - eapply all2_mono; [| exact H]. exact IH.
  - eapply all2_mono; [| exact H]. intros p q. rewrite !andb_true_iff. intros [E1 E2]. split; apply IH; assumption.
  - rewrite andb_true_iff in H |- *. destruct H as [E1 E2]. split; [exact E1 | apply IH; exact E2].
Qed.

Lemma eq_upto_le : forall g1 g2 k j a b, k <= j -> eq_upto g1 g2 j a b = true -> eq_upto g1 g2 k a b = true.
Proof.
  intros g1 g2 k j a b L. replace j with ((j - k) + k) by lia. generalize (j - k) as d. clear L.
  induction d as [| d IH]; intros H; [exact H |]. apply IH. apply eq_upto_down. exact H.
Qed.

Lemma all2_ext_in : forall A B (R R' : A -> B -> bool) l1 l2,
  (forall x y, In x l1 -> In y l2 -> R x y = R' x y) -> all2 R l1 l2 = all2 R' l1 l2.
Proof.
  intros A B R R'. induction l1 as [| x l1 IH]; intros [| y l2] H; simpl; try reflexivity.
  rewrite (H x y (or_introl eq_refl) (or_introl eq_refl)).
  rewrite IH; [reflexivity | intros x' y' H1 H2; apply H; right; assumption].
Qed.

Lemma In_children_map : forall (ps : list (nat * nat)) p,
  In p ps -> In (fst p) (children (NMap ps)) /\ In (snd p) (children (NMap ps)).
Proof.
  intros ps p H. simpl. split; apply in_flat_map; exists p; split; [exact H | left; reflexivity | exact H | right; left; reflexivity].
Qed.

Lemma node_eqb_ext : forall R R' n1 n2,
  (forall x y, In x (children n1) -> In y (children n2) -> R x y = R' x y) -> node_eqb R n1 n2 = node_eqb R' n1 n2.
Proof.
  intros R R' n1 n2 H. destruct n1 as [x1 | x1 | l1 | ps1 | t1 c1], n2 as [x2 | x2 | l2 | ps2 | t2 c2]; simpl; try reflexivity.
  - apply all2_ext_in. exact H.
  - apply all2_ext_in. intros p q Hp Hq.
    destruct (In_children_map ps1 p Hp) as [P1 P2]. destruct (In_children_map ps2 q Hq) as [Q1 Q2].
    rewrite (H _ _ P1 Q1), (H _ _ P2 Q2). reflexivity.
  - rewrite (H c1 c2 (or_introl eq_refl) (or_introl eq_refl)). reflexivity.
Qed.

Section Decision.

Variables g1 g2 : list (nat * node).
Hypothesis closed1 : closed g1.
Hypothesis closed2 : closed g2.

(* The comparison of depth S k gives the same answer as the one of depth k on all pairs of nodes. *)
Definition stable (k : nat) : Prop :=
  forall x y, In x (map fst g1) -> In y (map fst g2) -> eq_upto g1 g2 (S k) x y = eq_upto g1 g2 k x y.

Lemma stable_next : forall k, stable k -> stable (S k).
Proof.
  intros k H x y Hx Hy.
  change (eq_upto g1 g2 (S (S k)) x y) with
    (match assoc g1 x, assoc g2 y with
     | Some n1, Some n2 => node_eqb (eq_upto g1 g2 (S k)) n1 n2
     | None, None => true
     | _, _ => false
     end).
  change (eq_upto g1 g2 (S k) x y) with
    (match assoc g1 x, assoc g2 y with
     | Some n1, Some n2 => node_eqb (eq_upto g1 g2 k) n1 n2
     | None, None => true
     | _, _ => false
     end).
  destruct (assoc g1 x) as [n1 |] eqn:G1, (assoc g2 y) as [n2 |] eqn:G2; try reflexivity.
  apply node_eqb_ext. intros c d Hc Hd. apply H.
  - specialize (closed1 x n1 G1). rewrite Forall_forall in closed1. apply closed1. exact Hc.
  - specialize (closed2 y n2 G2). rewrite Forall_forall in closed2. apply closed2. exact Hd.
Qed.

Lemma stable_after : forall k, stable k -> forall j, k <= j ->
  forall x y, In x (map fst g1) -> In y (map fst g2) -> eq_upto g1 g2 j x y = eq_upto g1 g2 k x y.
Proof.
  intros k H j L. replace j with ((j - k) + k) by lia. generalize (j - k) as d. clear L.
  assert (Sd : forall d, stable (d + k)) by (induction d as [| d IHd]; [exact H | apply stable_next; exact IHd]).
  induction d as [| d IH]; intros x y Hx Hy; [reflexivity |].
  simpl (S d + k). rewrite (Sd d x y Hx Hy). apply IH; assumption.
Qed.

Definition pairs_of_keys : list (nat * nat) := list_prod (map fst g1) (map fst g2).

(* The count of the pairs of nodes whose unfoldings of depth k agree. *)
Definition agree_count (k : nat) : nat :=
  length (filter (fun p => eq_upto g1 g2 k (fst p) (snd p)) pairs_of_keys).

Lemma filter_le : forall A (f h : A -> bool) l,
  (forall x, f x = true -> h x = true) -> length (filter f l) <= length (filter h l).
Proof.
  intros A f h l H. induction l as [| x l IH]; simpl; [lia |].
  destruct (f x) eqn:F, (h x) eqn:G; simpl; try lia.
  rewrite (H x F) in G. discriminate.
Qed.

Lemma filter_le_length : forall A (f : A -> bool) l, length (filter f l) <= length l.
Proof. intros A f. induction l as [| x l IH]; simpl; [lia | destruct (f x); simpl; lia]. Qed.

Lemma filter_same_count : forall A (f h : A -> bool) l,
  (forall x, f x = true -> h x = true) -> length (filter f l) = length (filter h l) ->
  forall x, In x l -> h x = true -> f x = true.
Proof.
  intros A f h l H. induction l as [| a l IH]; intros E x Hx Hh; [destruct Hx |].
  simpl in E. assert (Le := filter_le A f h l H).
  destruct (f a) eqn:F, (h a) eqn:G; simpl in E.
  - destruct Hx as [<- | Hx]; [exact F | apply IH; [lia | exact Hx | exact Hh]].
  - rewrite (H a F) in G. discriminate.
  - lia.
  - destruct Hx as [<- | Hx]; [rewrite G in Hh; discriminate | apply IH; [lia | exact Hx | exact Hh]].
Qed.

Lemma agree_count_step : forall k, agree_count (S k) <= agree_count k.
Proof. intros k. apply filter_le. intros p. apply eq_upto_down. Qed.

Lemma decreasing_stops : forall n (c : nat -> nat),
  c 0 <= n -> (forall k, c (S k) <= c k) -> exists k, k <= n /\ c (S k) = c k.
Proof.
  induction n as [| n IH]; intros c L H.
  - exists 0. split; [lia |]. specialize (H 0). lia.
  - destruct (Nat.eq_dec (c 1) (c 0)) as [E | D]; [exists 0; split; [lia | exact E] |].
    destruct (IH (fun k => c (S k))) as [k [Lk Ek]]; [specialize (H 0); lia | intros k; apply H |].
    exists (S k). split; [lia | exact Ek].
Qed.

Lemma stable_exists : exists k, k <= length g1 * length g2 /\ stable k.
Proof.
  destruct (decreasing_stops (length g1 * length g2) agree_count) as [k [L E]].
  - unfold agree_count, pairs_of_keys. rewrite <- (length_map fst g1), <- (length_map fst g2), <- length_prod.
    apply filter_le_length.
  - exact agree_count_step.
  - exists k. split; [exact L |]. intros x y Hx Hy.
    destruct (eq_upto g1 g2 (S k) x y) eqn:A1, (eq_upto g1 g2 k x y) eqn:A2; try reflexivity.
    + apply eq_upto_down in A1. congruence.
    + assert (I : In (x, y) pairs_of_keys) by (apply in_prod; assumption).
      pose proof (filter_same_count _ (fun p => eq_upto g1 g2 (S k) (fst p) (snd p))
                    (fun p => eq_upto g1 g2 k (fst p) (snd p)) pairs_of_keys
                    (fun p => eq_upto_down g1 g2 k (fst p) (snd p)) E (x, y) I A2) as A3.
      change (eq_upto g1 g2 (S k) x y = true) in A3. congruence.
Qed.

End Decision.

(* T11. On closed graphs, same_key decides whether two nodes unfold alike at every depth. *)
Theorem same_key_correct : forall g1 a g2 b,
  closed g1 -> closed g2 -> In a (map fst g1) -> In b (map fst g2) ->
  same_key g1 a g2 b = true <-> bisimilar g1 a g2 b.
Proof.
  intros g1 a g2 b C1 C2 Ha Hb. unfold same_key, bisimilar. split.
  - intros H k. apply eq_upto_unfold.
    destruct (stable_exists g1 g2) as [k0 [L S0]].
    assert (H0 : eq_upto g1 g2 k0 a b = true)
      by (rewrite <- (stable_after g1 g2 C1 C2 k0 S0 (length g1 * length g2) L a b Ha Hb); exact H).
    destruct (Nat.le_gt_cases k0 k) as [Lk | Lk].
    + rewrite (stable_after g1 g2 C1 C2 k0 S0 k Lk a b Ha Hb). exact H0.
    + apply (eq_upto_le g1 g2 k k0); [lia | exact H0].
  - intros H. apply eq_upto_unfold. apply H.
Qed.

(* An array that holds itself and two arrays that hold each other unfold alike: they are the same key. *)
Example same_key_cycles : same_key [(0, NArr [0])] 0 [(1, NArr [2]); (2, NArr [1])] 1 = true.
Proof. reflexivity. Qed.

(* An array that holds itself and an array that holds an empty array differ at depth 2. *)
Example different_key : same_key [(0, NArr [0])] 0 [(1, NArr [2]); (2, NArr [])] 1 = false.
Proof. reflexivity. Qed.
